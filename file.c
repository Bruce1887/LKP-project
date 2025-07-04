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
		inode->i_blocks = 1;

		mark_buffer_dirty(bh_index);
		brelse(bh_index);
	}

	return 0;
}

static ssize_t custom_read_iter(struct kiocb *iocb, struct iov_iter *to)
{
	pr_info("%s: pos=%lld, count=%zu\n", __func__, iocb->ki_pos,
		iov_iter_count(to));

	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index = NULL, *bh_data = NULL;
	loff_t pos = iocb->ki_pos;
	size_t count = iov_iter_count(to);
	ssize_t ret = 0;
	ssize_t copied = 0;

	/* Check if read position is beyond file size */
	if (pos >= inode->i_size)
		return 0;

	pr_info("%s: pos=%lld, count=%zu\n", __func__, pos, count);
	/* Limit read to file size */
	if (pos + count > inode->i_size)
		count = inode->i_size - pos;

	if (count == 0)
		return 0;

	/* TODO: Check if we want to read a small or a big file (more/less than 128 bytes)
		Reading big files is implemented already using index blocks,
		Small files will be read from a sliced block.
	*/

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
	if (bh_index)
		brelse(bh_index);
	if (bh_data)
		brelse(bh_data);

	return ret;
}

static ssize_t custom_write_iter(struct kiocb *iocb, struct iov_iter *from)
{
	pr_info("%s: pos=%lld, count=%zu\n", __func__, iocb->ki_pos,
		iov_iter_count(from));

	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	struct ouichefs_file_index_block *index;
	struct buffer_head *bh_index = NULL, *bh_data = NULL;
	loff_t pos = iocb->ki_pos;
	size_t count = iov_iter_count(from);
	ssize_t ret = 0;
	ssize_t copied = 0;
	uint32_t nr_allocs = 0;
	loff_t old_size = inode->i_size;
	loff_t new_size;
	uint32_t old_blocks;

	/* Check file size limits */
	if (pos + count > OUICHEFS_MAX_FILESIZE) {
		count = OUICHEFS_MAX_FILESIZE - pos;
		if (count <= 0)
			return -ENOSPC;
	}

	/* Calculate new file size and required blocks */
	new_size = max((loff_t)(pos + count), old_size);
	pr_info("%s: old_size=%lld, new_size=%lld\n", __func__, old_size,
		new_size);

	/* TODO: Check if we want to wite a small or a big file (more/less than 128 bytes)
	Writing big files is already implemented.
	Writing small files requires first checking sbi->s_free_sliced_blocks to see if there already is a partially filled block.
	If there is, we can write our small file to it.
	If there is not, we need to allocate a new sliced block.
	We can use the get_free_block() function to allocate a new sliced block.
	In this new block, the first slice will contain a 32-bit bitmap
	indicating which slices are used (the first 32 bits of the slice).
	After this there will be a number indicating the next partially filled block.
	*/

	// if (new_size <= OUICHEFS_SLICE_SIZE) {
	//	/* We want to write a Small file */
	//	if (sbi->s_free_sliced_blocks == 0) {
	//		/* No sliced blocks available, allocate a new one */
	//		uint32_t sliced_block = get_free_block(sbi);
	//		if (!sliced_block) {
	//			return -ENOSPC;
	//		}
	//		sbi->s_free_sliced_blocks = sliced_block;
	//	} else {
	//		/* There is a sliced block already, lets write to it */
	//	}
	// }
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
	if (bh_index)
		brelse(bh_index);
	if (bh_data)
		brelse(bh_data);

	pr_info("size at end: %lld", inode->i_size);
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
