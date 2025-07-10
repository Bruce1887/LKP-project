// SPDX-License-Identifier: GPL-2.0
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */

#define pr_fmt(fmt) "%s:%s: " fmt, KBUILD_MODNAME, __func__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/mpage.h>
#include <linux/uio.h>
#include <linux/types.h>

#include "ouichefs.h"
#include "bitmap.h"

/*
 * Map the buffer_head passed in argument with the iblock-th block of the file
 * represented by inode. If the requested block is not allocated and create is
 * true, allocate a new block on disk and map it.
 */
static int ouichefs_file_get_block(struct inode *inode, sector_t iblock,
				   struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index;
	int ret = 0, bno;

	/* If block number exceeds filesize, fail */
	if (iblock >= OUICHEFS_BLOCK_SIZE >> 2)
		return -EFBIG;

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index)
		return -EIO;
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	/*
	 * Check if iblock is already allocated. If not and create is true,
	 * allocate it. Else, get the physical block number.
	 */
	if (index->blocks[iblock] == 0) {
		if (!create) {
			ret = 0;
			goto brelse_index;
		}
		bno = get_free_block(sbi);
		if (!bno) {
			ret = -ENOSPC;
			goto brelse_index;
		}
		index->blocks[iblock] = cpu_to_le32(bno);
		mark_buffer_dirty(bh_index);
	} else {
		bno = le32_to_cpu(index->blocks[iblock]);
	}

	/* Map the physical block to the given buffer_head */
	map_bh(bh_result, sb, bno);

brelse_index:
	brelse(bh_index);

	return ret;
}

/*
 * Called by the page cache to read a page from the physical disk and map it in
 * memory.
 */
static void ouichefs_readahead(struct readahead_control *rac)
{
	mpage_readahead(rac, ouichefs_file_get_block);
}

/*
 * Called by the page cache to write a dirty page to the physical disk (when
 * sync is called or when memory is needed).
 */
static int ouichefs_writepage(struct page *page, struct writeback_control *wbc)
{
	return block_write_full_page(page, ouichefs_file_get_block, wbc);
}

/*
 * Called by the VFS when a write() syscall occurs on file before writing the
 * data in the page cache. This functions checks if the write will be able to
 * complete and allocates the necessary blocks through block_write_begin().
 */
static int ouichefs_write_begin(struct file *file,
				struct address_space *mapping, loff_t pos,
				unsigned int len, struct page **pagep,
				void **fsdata)
{
	pr_info("%s:%d: pos=%lld, len=%u\n", __func__, __LINE__, pos, len);
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(file->f_inode->i_sb);
	int err;
	uint32_t nr_allocs = 0;

	/* Check if the write can be completed (enough space?) */
	if (pos + len > OUICHEFS_MAX_FILESIZE)
		return -ENOSPC;
	nr_allocs = max(pos + len, file->f_inode->i_size) / OUICHEFS_BLOCK_SIZE;
	if (nr_allocs > file->f_inode->i_blocks - 1)
		nr_allocs -= file->f_inode->i_blocks - 1;
	else
		nr_allocs = 0;
	if (nr_allocs > sbi->nr_free_blocks)
		return -ENOSPC;

	/* prepare the write */
	err = block_write_begin(mapping, pos, len, pagep,
				ouichefs_file_get_block);
	/* if this failed, reclaim newly allocated blocks */
	if (err < 0) {
		pr_err("%s:%d: newly allocated blocks reclaim not implemented yet\n",
		       __func__, __LINE__);
	}
	return err;
}

/*
 * Called by the VFS after writing data from a write() syscall to the page
 * cache. This functions updates inode metadata and truncates the file if
 * necessary.
 */
static int ouichefs_write_end(struct file *file, struct address_space *mapping,
			      loff_t pos, unsigned int len, unsigned int copied,
			      struct page *page, void *fsdata)
{
	pr_info("%s:%d: pos=%lld, len=%u\n", __func__, __LINE__, pos, len);
	int ret;
	struct inode *inode = file->f_inode;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct super_block *sb = inode->i_sb;

	/* Complete the write() */
	ret = generic_write_end(file, mapping, pos, len, copied, page, fsdata);
	if (ret < len) {
		pr_err("%s:%d: wrote less than asked... what do I do? nothing for now...\n",
		       __func__, __LINE__);
	} else {
		uint32_t nr_blocks_old = inode->i_blocks;

		/* Update inode metadata */
		inode->i_blocks = (roundup(inode->i_size, OUICHEFS_BLOCK_SIZE) /
				   OUICHEFS_BLOCK_SIZE) +
				  1;
		inode->i_mtime = inode->i_ctime = current_time(inode);
		mark_inode_dirty(inode);

		/* If file is smaller than before, free unused blocks */
		if (nr_blocks_old > inode->i_blocks) {
			int i;
			struct buffer_head *bh_index;
			struct ouichefs_file_index_block *index;

			/* Free unused blocks from page cache */
			truncate_pagecache(inode, inode->i_size);

			/* Read index block to remove unused blocks */
			bh_index = sb_bread(sb, ci->index_block);
			if (!bh_index) {
				pr_err("failed truncating '%s'. we just lost %llu blocks\n",
				       file->f_path.dentry->d_name.name,
				       nr_blocks_old - inode->i_blocks);
				goto end;
			}
			index = (struct ouichefs_file_index_block *)
					bh_index->b_data;

			for (i = inode->i_blocks - 1; i < nr_blocks_old - 1;
			     i++) {
				put_block(OUICHEFS_SB(sb),
					  le32_to_cpu(index->blocks[i]));
				index->blocks[i] = 0;
			}
			mark_buffer_dirty(bh_index);
			brelse(bh_index);
		}
	}
end:
	return ret;
}

const struct address_space_operations ouichefs_aops = {
	.readahead = ouichefs_readahead,
	.writepage = ouichefs_writepage,
	.write_begin = ouichefs_write_begin,
	.write_end = ouichefs_write_end
};

static int ouichefs_open(struct inode *inode, struct file *file)
{
	bool wronly = (file->f_flags & O_WRONLY) != 0;
	bool rdwr = (file->f_flags & O_RDWR) != 0;
	bool trunc = (file->f_flags & O_TRUNC) != 0;

	if ((wronly || rdwr) && trunc && (inode->i_size != 0)) {
		struct super_block *sb = inode->i_sb;
		struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
		struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
		struct ouichefs_file_index_block *index;
		struct buffer_head *bh_index;
		sector_t iblock;

		/* Read index block from disk */
		bh_index = sb_bread(sb, ci->index_block);
		if (!bh_index)
			return -EIO;
		index = (struct ouichefs_file_index_block *)bh_index->b_data;

		for (iblock = 0; index->blocks[iblock] != 0; iblock++) {
			put_block(sbi, le32_to_cpu(index->blocks[iblock]));
			index->blocks[iblock] = 0;
		}
		inode->i_size = 0;

		mark_buffer_dirty(bh_index);
		brelse(bh_index);
	}

	return 0;
}

static bool is_small_file(struct inode *inode) {
	return inode->i_blocks == 0;
}

static ssize_t custom_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	// struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index = NULL, *bh_data = NULL;
	loff_t pos = iocb->ki_pos;
	size_t count = iov_iter_count(to);
	ssize_t ret = 0;
	ssize_t copied = 0;

	pr_info("pos=%lld, count=%zu, inode->i_size=%lld\n", (long long)pos,
		count, (long long)inode->i_size);

	/* Check if read position is beyond file size */
	if (pos >= inode->i_size) {
		pr_info("pos is beyond file size, returning 0\n");
		return 0;
	}

	/* Limit read to file size */
	if (pos + count > inode->i_size)
		count = inode->i_size - pos;

	if (count == 0)
		return 0;

	/* Check if index block has NOT yet been set */
	if (ci->index_block == 0) {
		/* We should never reach this. If trying to read a file that has not had any data written to it,
		that case should be caught when checking the read position vs the file size previously in this function.
		*/
		pr_err("index_block == 0, this should not happen!\n");
		return 0;
	}

	/* Check if we want to read a small or a big file (more/less than 128 bytes)
		Reading big files is implemented already using index blocks,
		Small files will be read from a sliced block.
	*/
	if (is_small_file(inode)) {
		pr_info("Reading small file\n");
	} else {
		pr_info("Reading big file\n");
		goto BIG_FILE;
	}

	uint32_t bno = OUICHEFS_SMALL_FILE_GET_BNO(ci);
	uint32_t slice_no = OUICHEFS_SMALL_FILE_GET_SLICE(ci);

	bh_data = sb_bread(sb, bno);
	if (!bh_data) {
		pr_err("Failed to read sliced block %u\n", bno);
		ret = -EIO;
		goto out;
	}

	pr_info("Reading small file: bno=%u, slice_no=%u\n", bno, slice_no);

	/* Check if slice_no is valid */
	if (slice_no == 0 ||
	    slice_no >= OUICHEFS_SLICE_SIZE / sizeof(uint32_t)) {
		pr_err("Slice number %u out of range\n", slice_no);
		ret = -EIO;
		brelse(bh_data);
		goto out;
	}

	pr_info("slice bitmap: %u\n", OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh_data));

	/* Copy data from user space */
	if (copy_to_iter(bh_data->b_data + slice_no * OUICHEFS_SLICE_SIZE,
			 count, to) != count) {
		brelse(bh_data);
		ret = -EFAULT;
		goto out;
	}

	/* Update file position */
	/* We can always read the entire file if we wish to if its a small file, 
	thus "pos" is unnecessary here and we just set position to count */
	iocb->ki_pos = count;
	ret = count;

	goto out;
BIG_FILE:

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index) {
		pr_err("%s: failed to read index block\n", __func__);

		ret = -EIO;
		goto out;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	while (count > 0) {
		/* Calculate which block and offset within block */
		sector_t block_idx = pos / OUICHEFS_BLOCK_SIZE;
		size_t block_offset = pos % OUICHEFS_BLOCK_SIZE;
		size_t to_read = min(count, OUICHEFS_BLOCK_SIZE - block_offset);
		uint32_t physical_block;

		/* Check if block index is valid */
		if (block_idx >= OUICHEFS_BLOCK_SIZE >> 2) {
			ret = -EFBIG;
			goto out;
		}

		/* Get physical block number */
		physical_block = le32_to_cpu(index->blocks[block_idx]);
		if (physical_block == 0) {
			/* Unallocated block - fill with zeros */
			if (iov_iter_zero(to_read, to) != to_read) {
				ret = -EFAULT;
				goto out;
			}
		} else {
			/* Read from allocated block */
			bh_data = sb_bread(sb, physical_block);
			if (!bh_data) {
				pr_err("%s: failed to read data block %u\n",
				       __func__, physical_block);
				ret = -EIO;
				goto out;
			}

			/* Copy data to user space */
			if (copy_to_iter(bh_data->b_data + block_offset,
					 to_read, to) != to_read) {
				brelse(bh_data);
				ret = -EFAULT;
				goto out;
			}

			brelse(bh_data);
			bh_data = NULL;
		}

		/* Update counters */
		pos += to_read;
		count -= to_read;
		copied += to_read;
	}

	/* Update file position */
	iocb->ki_pos = pos;
	ret = copied;

out:
	pr_info("at out section\n");
	pr_info("bh_index: %p, bh_data: %p, ret: %ld\n", bh_index, bh_data,
		ret);

	if (bh_index)
		brelse(bh_index);
	if (bh_data)
		brelse(bh_data);

	return ret;
}

static struct buffer_head *init_slice_block(struct super_block *sb,
					    uint32_t block)
{
	uint32_t physical_block = le32_to_cpu(block);
	struct buffer_head *bh_data = sb_bread(sb, physical_block);
	if (!bh_data) {
		return NULL;
	}

	/* initialie metadata */
	bh_data->b_data[0] = (char)~(0b1);
	bh_data->b_data[1] = (char)~0;
	bh_data->b_data[2] = (char)~0;
	bh_data->b_data[3] = (char)~0;

	return bh_data;
}

static ssize_t allocate_and_init_slice_block(struct super_block *sb,
					     struct ouichefs_sb_info *sbi,
					     struct buffer_head **bh_data)
{
	uint32_t free_block = get_free_block(sbi);
	if (!free_block || free_block > 1 << 27) {
		pr_err("Failed to allocate sliced block\n");
		return -ENOSPC;
	}
	*bh_data = init_slice_block(sb, free_block);

	if (!*bh_data) {
		pr_err("Failed to initialize sliced block\n");
		put_block(sbi, free_block);
		brelse(*bh_data);
		*bh_data = NULL;
		return -EIO;
	}
	return (ssize_t)free_block;
}

static bool is_new(uint32_t index_block) {
	return index_block == 0;
}

static bool will_be_small(loff_t new_size) {
	return new_size <= OUICHEFS_SLICE_SIZE;
}

static ssize_t write_big_file(
	struct inode *inode,
	struct ouichefs_inode_info *ci,
	struct super_block *sb,
	struct ouichefs_sb_info * sbi,
	struct kiocb *iocb,
	struct iov_iter *from
);

static ssize_t write_small_file(
	struct inode *inode,
	struct ouichefs_inode_info *ci,
	struct super_block *sb,
	struct ouichefs_sb_info * sbi,
	struct kiocb *iocb,
	struct iov_iter *from
);

static ssize_t custom_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	loff_t pos = iocb->ki_pos;
	size_t count = iov_iter_count(from);
	loff_t old_size = inode->i_size;
	loff_t new_size = max((loff_t)(pos + count), old_size);

	pr_info("pos: %lld, flags: %d", pos, iocb->ki_flags);

	if (is_new(ci->index_block)) {
		if (will_be_small(new_size)) {
			pr_info("using write_small_file implementation\n");
			return write_small_file(inode, ci, sb, sbi, iocb, from);
		} else {
			pr_info("using write_big_file implementation\n");
			return write_big_file(inode, ci, sb, sbi, iocb, from);
		}
	} else {
		if(!is_small_file(&ci->vfs_inode)) {
			pr_info("using write_big_file implementation\n");
			return write_big_file(inode, ci, sb, sbi, iocb, from);
		} else if (will_be_small(new_size)) {
			pr_info("using write_small_file implementation\n");
			return write_small_file(inode, ci, sb, sbi, iocb, from);
		} else {

		}
	}

	return 0;
}

static ssize_t write_big_file(
	struct inode *inode,
	struct ouichefs_inode_info *ci,
	struct super_block *sb,
	struct ouichefs_sb_info * sbi,
	struct kiocb *iocb,
	struct iov_iter *from
) {
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index = NULL, *bh_data = NULL;
	size_t count = iov_iter_count(from);
	loff_t pos = iocb->ki_pos;
	if (iocb->ki_flags & IOCB_APPEND) {
		pos = inode->i_size;
	}

	ssize_t ret = 0;
	ssize_t copied = 0;
	uint32_t nr_allocs = 0;
	loff_t old_size = inode->i_size;
	loff_t new_size = max((loff_t)(pos + count), old_size);
	uint32_t old_blocks;


	/* Check if this inode's index_block field has NOT yet been set */
	if (ci->index_block == 0) {
		/* This is a large file without an index block. We need to allocate an index block */
		__le32 bno = get_free_block(sbi);
		if (!bno) {
			pr_err("Failed to allocate index block\n");
			// put_inode(sbi, ci->vfs_inode.i_ino);
			ret = -ENOSPC;
			goto out;
		}
		ci->index_block = bno;
	}

	/* Calculate new file size and required blocks */
	new_size = max((loff_t)(pos + count), old_size);
	pr_info("%s: old_size=%lld, new_size=%lld\n", __func__, old_size,
		new_size);

	pr_info("%s: inode.index_block: %u\n", __func__, ci->index_block);

	/* Calculate required blocks for file (size > 128 bytes) */
	nr_allocs = DIV_ROUND_UP(new_size, OUICHEFS_BLOCK_SIZE);

	/* Check if we have enough free blocks */
	if (nr_allocs > inode->i_blocks - 1) {
		/* We need more blocks */
		uint32_t blocks_needed = nr_allocs - (inode->i_blocks - 1);
		if (blocks_needed > sbi->nr_free_blocks) {
			return -ENOSPC;
		}
	}

	/* Read index block from disk */
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index) {
		ret = -EIO;
		goto out;
	}
	index = (struct ouichefs_file_index_block *)bh_index->b_data;

	old_blocks = inode->i_blocks;

	while (count > 0) {
		/* Calculate which block and offset within block */
		sector_t block_idx = pos / OUICHEFS_BLOCK_SIZE;
		size_t block_offset = pos % OUICHEFS_BLOCK_SIZE;
		size_t to_write =
			min(count, OUICHEFS_BLOCK_SIZE - block_offset);
		uint32_t physical_block;

		/* Check if block index is valid */
		if (block_idx >= OUICHEFS_BLOCK_SIZE >> 2) {
			ret = -EFBIG;
			goto out;
		}

		/* Get or allocate physical block */
		physical_block = le32_to_cpu(index->blocks[block_idx]);
		if (physical_block == 0) {
			/* Allocate new block */
			physical_block = get_free_block(sbi);
			if (!physical_block) {
				ret = -ENOSPC;
				goto out;
			}
			index->blocks[block_idx] = cpu_to_le32(physical_block);
			mark_buffer_dirty(bh_index);
		}

		/* Read the block (we might be doing partial write) */
		bh_data = sb_bread(sb, physical_block);
		if (!bh_data) {
			ret = -EIO;
			goto out;
		}

		/* If writing beyond current file size, zero the gap */
		if (pos > inode->i_size) {
			loff_t gap_start = inode->i_size;
			loff_t gap_end = pos;

			/* Zero the gap in this block if needed */
			if (gap_start / OUICHEFS_BLOCK_SIZE == block_idx) {
				size_t gap_offset =
					gap_start % OUICHEFS_BLOCK_SIZE;
				size_t gap_size =
					min(gap_end - gap_start,
					    (loff_t)(OUICHEFS_BLOCK_SIZE -
						     gap_offset));
				memset(bh_data->b_data + gap_offset, 0,
				       gap_size);
			}
		}

		/* Copy data from user space */
		if (copy_from_iter(bh_data->b_data + block_offset, to_write,
				   from) != to_write) {
			brelse(bh_data);
			ret = -EFAULT;
			goto out;
		}

		/* Mark buffer dirty and sync */
		mark_buffer_dirty(bh_data);
		sync_dirty_buffer(bh_data);
		brelse(bh_data);
		bh_data = NULL;

		/* Update counters */
		pos += to_write;
		count -= to_write;
		copied += to_write;
	}
	pr_info("%s: wrote %zd bytes at pos %lld\n", __func__, copied, pos);
	/* Update inode metadata */
	if (pos > inode->i_size) {
		inode->i_size = pos;
	}

	/* Update block count */
	inode->i_blocks = DIV_ROUND_UP(inode->i_size, OUICHEFS_BLOCK_SIZE) + 1;
	inode->i_mtime = inode->i_ctime = current_time(inode);
	mark_inode_dirty(inode);

	/* Update file position */
	iocb->ki_pos = pos;
	ret = copied;

	/* Sync index block */
	sync_dirty_buffer(bh_index);

out:

	pr_info("at out section\n");
	pr_info("bh_index: %p, bh_data: %p, ret: %ld\n", bh_index, bh_data,
		ret);

	if (bh_index)
		brelse(bh_index);
	if (bh_data)
		brelse(bh_data);

	return ret;
}

static ssize_t write_small_file(
	struct inode *inode,
	struct ouichefs_inode_info *ci,
	struct super_block *sb,
	struct ouichefs_sb_info * sbi,
	struct kiocb *iocb,
	struct iov_iter *from
) {
	struct buffer_head *bh_index = NULL, *bh_data = NULL;
	size_t count = iov_iter_count(from);
	loff_t pos = iocb->ki_pos;
	if (iocb->ki_flags & IOCB_APPEND) {
		pos = inode->i_size;
	}
	ssize_t ret = 0;
	uint32_t block_to_write = 0;
	uint32_t slice_to_write = 0;
	loff_t old_size = inode->i_size;
	loff_t new_size = max((loff_t)(pos + count), old_size);

	/* Check if this inode's index_block field has NOT yet been set */
	if (ci->index_block == 0) {
		/* This is a small file that has not yet been added to a partially filled block.
			Try to find a slice for it. */
		if (sbi->s_free_sliced_blocks == 0) {
			/* No sliced blocks available, allocate a new one and read it into bh_data */
			block_to_write = allocate_and_init_slice_block(
				sb, sbi, &bh_data);
			if (block_to_write <= 0) { /* 0 is not a valid block*/
				ret = block_to_write;
				goto out;
			}
			sbi->s_free_sliced_blocks = (uint32_t)block_to_write;
			pr_info("Allocated new sliced block: %u\n",
				block_to_write);

		} else {
			/* There already is a sliced block, read it. */
			block_to_write = sbi->s_free_sliced_blocks;
			bh_data = sb_bread(sb, block_to_write);
			if (!bh_data) {
				pr_err("Failed to read sliced block\n");
				ret = -EIO;
				goto out;
			}
			pr_info("Using existing sliced block: %u\n",
				sbi->s_free_sliced_blocks);
		}

		pr_info("bitmap before: %u\n",
			OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh_data));

		slice_to_write = OUICHEFS_GET_FIRST_FREE_BIT(bh_data);

		pr_info("slice_to_write: %u\n", slice_to_write);

		pr_info("bitmap after: %u\n",
			OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh_data));

		/* slice_to_write is 0 if no free bit found (we assume that the first bit is never free) */
		while (slice_to_write == 0) {
			/* This loop uses bh_data to store a reference to the current sliced block, and bh_index for the previous sliced block */

			/* Try fetch reference to next sliced block (if any exists) */
			block_to_write = OUICHEFS_SLICED_BLOCK_SB_NEXT(bh_data);

			/* store a reference to the previous sliced block */
			bh_index = bh_data;

			if (block_to_write == 0) {
				/* No next sliced block, allocate a new one */
				ssize_t free_block =
					allocate_and_init_slice_block(sb, sbi,
								      &bh_data);
				if (free_block <=
				    0) { /* 0 is not a valid block, hence "<= 0"*/
					ret = free_block;
					goto out;
				}
				pr_info("Allocated new sliced block: %lu\n",
					free_block);

				/* set pointer of previous block to this block */
				OUICHEFS_SLICED_BLOCK_SB_SET_NEXT(bh_index,
								  free_block);

				/* we are now finished with the previous block*/
				mark_buffer_dirty(bh_index);
				sync_dirty_buffer(bh_index);
				brelse(bh_index);
				bh_index = NULL;
			}

			pr_info("bitmap as uint32_t: %u\n",
				OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh_data));

			/* Check if this block has any empty slices */
			slice_to_write = OUICHEFS_GET_FIRST_FREE_BIT(bh_data);

			pr_info("slice_to_write: %u\n", slice_to_write);

			pr_info("bitmap as uint32_t: %u\n",
				OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh_data));

			if (slice_to_write == 0 && !bh_index)
				panic("This can only occur if we allocated a new sliced block, but it has no empty slices. This should never happen.");

			/* Cleanup any references to previous block */
			if (bh_index) {
				brelse(bh_index);
				bh_index = NULL;
			}
		}
	} else {
		/* This is a small file that has already been added to a partially filled block.
			We can use the index_block field to find the block and slice to write to. */
		pr_info("This is a small file that has already been added to a sliced block.\n");
		block_to_write = OUICHEFS_SMALL_FILE_GET_BNO(ci);
		slice_to_write = OUICHEFS_SMALL_FILE_GET_SLICE(ci);

		pr_info("block_to_write: %u, slice_to_write: %u\n",
			block_to_write, slice_to_write);

		if (block_to_write == 0 || slice_to_write == 0) {
			pr_err("block_to_write or slice_to_write is 0, this should not happen!\n");
			ret = -EIO;
			goto out;
		}

		bh_data = sb_bread(sb, block_to_write);
		if (!bh_data) {
			pr_err("Failed to read sliced block %u\n",
			       block_to_write);
			ret = -EIO;
			goto out;
		}
	}

	if (!bh_data) {
		/* We should have found a slice by now. If bh_data is null it means we have not. */
		pr_err("bh_data is NULL, this should not happen!\n");
		ret = -EIO;
		goto out;
	}

	/* Copy data from user space */
	if (copy_from_iter(bh_data->b_data +
				   slice_to_write * OUICHEFS_SLICE_SIZE + pos,
			   count, from) != count) {
		brelse(bh_data);
		ret = -EFAULT;
		goto out;
	}

	/* Mark buffer dirty and sync */
	mark_buffer_dirty(bh_data);
	sync_dirty_buffer(bh_data);
	brelse(bh_data);
	bh_data = NULL;

	pr_info("buffer marked dirty and syncing\n");
	ret = count;

	pr_info("wrote %zd bytes at pos %lld\n", count, pos);

	/* Update inode metadata */
	inode->i_size = new_size;
	inode->i_mtime = inode->i_ctime = current_time(inode);

	ci->index_block = (block_to_write << 5) + slice_to_write;
	mark_inode_dirty(inode);

	pr_info("inode metadata updated\n");

	/* Update file position */
	iocb->ki_pos = pos;

	goto out;
out:

	pr_info("at out section\n");
	pr_info("bh_index: %p, bh_data: %p, ret: %ld\n", bh_index, bh_data,
		ret);

	if (bh_index)
		brelse(bh_index);
	if (bh_data)
		brelse(bh_data);

	return ret;
}

const struct file_operations ouichefs_file_ops = {
	.owner = THIS_MODULE,
	.open = ouichefs_open,
	.llseek = generic_file_llseek,
	.read_iter = custom_read_iter,
	.write_iter = custom_write_iter,
	// .read_iter = generic_file_read_iter,
	// .write_iter = generic_file_write_iter,
	.fsync = generic_file_fsync,
};
