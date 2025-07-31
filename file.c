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

static bool is_small_file(struct inode *inode)
{
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

	pr_info("NEW READ CALL! pos=%lld, count=%zu, inode->i_size=%lld\n",
		(long long)pos, count, (long long)inode->i_size);

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

	/* Check if slice_no is valid */
	if (slice_no == 0 ||
	    slice_no >= OUICHEFS_SLICE_SIZE / sizeof(uint32_t)) {
		pr_err("Slice number %u out of range\n", slice_no);
		ret = -EIO;
		brelse(bh_data);
		goto out;
	}

	pr_info("slice bitmap: %u\n", OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh_data));

	/* TODO: DO WE READ ALL THE SLICES FOR THIS FILE? */
	/* Copy data to user space */
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

	/* Check if index block has NOT yet been set */
	if (ci->index_block == 0) {
		/* We should never reach this. If trying to read a file that has not had any data written to it,
		that case should be caught when checking the read position vs the file size previously in this function.
		*/
		pr_err("index_block == 0, this should not happen!\n");
		return 0;
	}

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
	if (physical_block == 0) {
		pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
		dump_stack();
	}
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
	if (!free_block || free_block > (1 << 27)) {
		pr_err("Failed to allocate sliced block. free_block: %u\n",
		       free_block);
		put_block(sbi, free_block);
		return -ENOSPC;
	}
	*bh_data = init_slice_block(sb, free_block);

	if (!*bh_data) {
		pr_err("Failed to initialize sliced block\n");
		put_block(sbi, free_block);
		return -EIO;
	}

	sbi->nr_sliced_blocks++;

	pr_info("Allocated new sliced block: %u. num sliced blocks: %u\n",
		free_block, sbi->nr_sliced_blocks);

	return (ssize_t)free_block;
}

static bool is_new(uint32_t index_block)
{
	return index_block == 0;
}

static bool will_be_small(loff_t new_size)
{
	return new_size <= OUICHEFS_BLOCK_SIZE - OUICHEFS_SLICE_SIZE;
}

static ssize_t write_big_file(struct inode *inode,
			      struct ouichefs_inode_info *ci,
			      struct super_block *sb,
			      struct ouichefs_sb_info *sbi, struct kiocb *iocb,
			      struct iov_iter *from);

static ssize_t write_small_file(struct inode *inode,
				struct ouichefs_inode_info *ci,
				struct super_block *sb,
				struct ouichefs_sb_info *sbi,
				struct kiocb *iocb, struct iov_iter *from);

static ssize_t delete_slice(struct super_block *sb,
			    struct ouichefs_sb_info *sbi, uint32_t bno,
			    uint32_t slice_no, uint32_t num_slices)
{
	/* Check if this small file actually has any data written to it */
	if (bno == 0)
		return 0;

	if (num_slices == 0) {
		pr_err("num_slices is 0. BAD!\n");
		return 0;
	}
	struct buffer_head *bh = NULL;

	if (bno == 0) {
		pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
		dump_stack();
	}
	bh = sb_bread(sb, bno);
	if (!bh) {
		pr_err("cannot read slice block %u\n", bno);
		return -EIO;
	}

	uint32_t mask = ((1U << num_slices) - 1) << (slice_no);

	/* Mark the slices the file used as free*/
	*(uint32_t *)bh->b_data |= mask;

	/* Zero out the slices for the small file */
	memset(bh->b_data + slice_no * OUICHEFS_SLICE_SIZE, 0,
	       num_slices * OUICHEFS_SLICE_SIZE);

	pr_info("Deleting slice %u from block %u, num_slices: %u\n", slice_no,
		bno, num_slices);
	sbi->nr_used_slices -= num_slices;
	pr_info("sbi->nr_used_slices: %u\n", sbi->nr_used_slices);

	mark_buffer_dirty(bh);
	brelse(bh);
	bh = NULL;

	/* Iterate over all sliced block
	   Check if blocks are empty and if so free and repoint pointers
	*/
	uint32_t current_bno = sbi->s_free_sliced_blocks;
	struct buffer_head *bh_prev = NULL;
	while (current_bno) {
		pr_info("current_bno: %u\n", current_bno);
		/* Read next sliced block */
		bh = sb_bread(sb, current_bno);
		if (!bh) {
			pr_err("Failed to read next sliced block %u\n",
			       current_bno);
			brelse(bh_prev);
			return -EIO;
		}

		/* Fetch next_bno now (we might scrub the block later)*/
		uint32_t next_bno = OUICHEFS_SLICED_BLOCK_SB_NEXT(bh);

		/* Check if block is free */
		if (OUICHEFS_BITMAP_IS_ALL_FREE(bh)) {
			pr_info("sliced block %llu is completely free, freeing it\n",
				bh->b_blocknr);

			/* Repoint "pointers" */
			if (bh_prev) {
				OUICHEFS_SLICED_BLOCK_SB_SET_NEXT(bh_prev,
								  next_bno);
				pr_info("Setting next_bno %u in previous block %llu\n",
					next_bno, bh_prev->b_blocknr);
				mark_buffer_dirty(bh_prev);
			} else {
				pr_info("No previous sliced block, setting s_free_sliced_blocks to %u\n",
					next_bno);
				sbi->s_free_sliced_blocks = next_bno;
			}

			/* Cleanup bh */
			sbi->nr_sliced_blocks--;
			pr_info("sbi->nr_sliced_blocks: %u\n",
				sbi->nr_sliced_blocks);
			memset(bh->b_data, 0, OUICHEFS_BLOCK_SIZE);
			put_block(sbi, bh->b_blocknr);
			mark_buffer_dirty(bh);
			brelse(bh);
			bh = NULL;
		} else {
			/* Sliced block not empty, go to next */
			pr_info("sliced block %llu is not empty, keeping it\n",
				bh->b_blocknr);

			/* Release reference to previous block (if there is one) */
			if (bh_prev) {
				brelse(bh_prev);
				bh_prev = NULL;
			}

			bh_prev = bh;
			bh = NULL;
		}
		current_bno = next_bno;
	}

	pr_info("at end: bh: %p, bh_prev: %p\n", bh, bh_prev);

	if (bh) {
		brelse(bh);
		bh = NULL;
	}
	if (bh_prev) {
		brelse(bh_prev);
		bh_prev = NULL;
	}

	return 0;
}

ssize_t delete_slice_and_clear_inode(struct ouichefs_inode_info *ci,
				     struct super_block *sb,
				     struct ouichefs_sb_info *sbi)
{
	ssize_t ret = 0;

	pr_info("inode index_block: %u, bno %u and slice_no %u\n",
		OUICHEFS_INODE(&ci->vfs_inode)->index_block,
		OUICHEFS_SMALL_FILE_GET_BNO(ci),
		OUICHEFS_SMALL_FILE_GET_SLICE(ci));
	uint32_t bno = OUICHEFS_SMALL_FILE_GET_BNO(ci);
	uint32_t slice_no = OUICHEFS_SMALL_FILE_GET_SLICE(ci);
	uint32_t num_slices = ci->num_slices;
	ret = delete_slice(sb, sbi, bno, slice_no, num_slices);
	if (ret < 0) {
		pr_err("Failed to delete slice: %zd\n", ret);
		return ret;
	}

	pr_info("Slice deleted successfully\n\n");

	/* Reset index block */
	ci->index_block = 0;
	ci->vfs_inode.i_size = 0;

	mark_inode_dirty(&ci->vfs_inode);

	return 0;
}

/* TODO: This function reads the data in the small file's slice, combines it with input from user-space and writes it using write_big_file(). I write big file fails, the small slice will still be removed! 
Solution 1: Dont care.
Solution 2: put necessary funtionality from delete_slice() in here (ugly but should work) */
static ssize_t convert_small_to_big(struct kiocb *iocb, struct iov_iter *from)
{
	struct file *file = iocb->ki_filp;
	struct inode *inode = file_inode(file);
	struct super_block *sb = inode->i_sb;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
	loff_t pos = iocb->ki_pos;
	loff_t count = iov_iter_count(from);

	/*	Stor old data about the file incase we fail to write a big one */
	loff_t old_size = inode->i_size;
	uint32_t old_index_block = ci->index_block;
	loff_t old_pos = pos;
	pr_info("Converting small file to big file. count: %lld, pos: %lld, inode->i_size: %lld\n",
		count, pos, inode->i_size);

	ssize_t ret = 0;
	char *combined_buf = NULL;
	struct kvec *kv = NULL;
	struct iov_iter new_iter;
	loff_t write_pos = pos;
	struct buffer_head *bh_data = NULL;

	if (iocb->ki_flags & IOCB_APPEND) {
		write_pos = old_size;
	}

	/* Always read the existing small file data first */
	uint32_t bno = OUICHEFS_SMALL_FILE_GET_BNO(ci);
	if (bno == 0) {
		pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
		dump_stack();
	}
	bh_data = sb_bread(sb, bno);
	if (!bh_data) {
		pr_err("Failed to read sliced block %u\n", bno);
		ret = -EIO;
		goto out;
	}

	/* Calculate the total size of the new file */
	size_t new_file_size = max(write_pos + count, old_size);

	/* Allocate buffer for the entire new file content */
	combined_buf = kmalloc(new_file_size, GFP_KERNEL);
	if (!combined_buf) {
		brelse(bh_data);
		ret = -ENOMEM;
		goto out;
	}

	uint32_t slice_no = OUICHEFS_SMALL_FILE_GET_SLICE(ci);

	/* Copy existing file data to the beginning of the buffer */
	memcpy(combined_buf, bh_data->b_data + slice_no * OUICHEFS_SLICE_SIZE,
	       old_size);
	brelse(bh_data);

	/* Zero-fill any gap between old data and write position */
	if (write_pos > old_size) {
		memset(combined_buf + old_size, 0, write_pos - old_size);
	}

	/* Copy new data from user space into the buffer */
	size_t copied = copy_from_iter(combined_buf + write_pos, count, from);
	if (copied != count) {
		pr_err("Failed to copy user data from iterator\n");
		ret = -EFAULT;
		goto out;
	}

	/* Create kvec and iterator for the entire new file content */
	kv = kmalloc(sizeof(struct kvec), GFP_KERNEL);
	if (!kv) {
		ret = -ENOMEM;
		goto out;
	}
	kv->iov_base = combined_buf;
	kv->iov_len = new_file_size;

	/* Create iterator that contains the complete new file */
	iov_iter_kvec(&new_iter, READ, kv, 1, new_file_size);
	new_iter.data_source = 1;

	/* Reset inode size to 0 since we're creating a new file */
	inode->i_size = 0;

	/* Reset position to 0 and remove append flag - we're writing entire file */
	struct kiocb new_iocb = *iocb;
	new_iocb.ki_pos = 0;
	new_iocb.ki_flags &= ~IOCB_APPEND;

	/* Write the entire new file content */
	ret = write_big_file(inode, ci, sb, sbi, &new_iocb, &new_iter);

	if (ret > 0) {
		/* Update the original iocb position to reflect the write position */
		iocb->ki_pos = write_pos + count;
		/* Return the number of bytes actually written by the user */
		ret = count;

		/* We ignore the return value of deleting the slice here. If data is lost then so be it.*/
		if (OUICHEFS_SMALL_FILE_GET_BNO(ci)) {
			pr_err("bno is 0");
		}
		uint32_t bno = OUICHEFS_SMALL_FILE_GET_BNO(ci);
		uint32_t slice_no = OUICHEFS_SMALL_FILE_GET_SLICE(ci);
		uint32_t num_slices = ci->num_slices;
		ret = delete_slice(sb, sbi, bno, slice_no, num_slices);
	} else {
		pr_err("Failed to write big file: %zd\n", ret);
		/* If writing failed, restore the old inode */
		ci->index_block = old_index_block;
		inode->i_size = old_size;
		iocb->ki_pos = old_pos;
	}

out:
	if (combined_buf) {
		kfree(combined_buf);
	}
	if (kv) {
		kfree(kv);
	}
	return ret;
}

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

	pr_info("NEW WRITE CALL! pos: %lld, flags: %d, count: %lu, inode num slices: %d",
		pos, iocb->ki_flags, count, ci->num_slices);

	if (is_new(ci->index_block)) {
		/* Writing to a file that has never been written to */
		if (will_be_small(new_size)) {
			return write_small_file(inode, ci, sb, sbi, iocb, from);
		} else {
			return write_big_file(inode, ci, sb, sbi, iocb, from);
		}
	} else {
		/* Writing to a file has previously been written to */
		if (!is_small_file(&ci->vfs_inode)) {
			return write_big_file(inode, ci, sb, sbi, iocb, from);
		} else if (will_be_small(new_size)) {
			return write_small_file(inode, ci, sb, sbi, iocb, from);
		} else if (!will_be_small(new_size) &&
			   is_small_file(&ci->vfs_inode)) {
			return convert_small_to_big(iocb, from);
		}
	}
	return -EINVAL;
}

static ssize_t write_big_file(struct inode *inode,
			      struct ouichefs_inode_info *ci,
			      struct super_block *sb,
			      struct ouichefs_sb_info *sbi, struct kiocb *iocb,
			      struct iov_iter *from)
{
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
	pr_info("old_size=%lld, new_size=%lld, index_block: %u\n", old_size,
		new_size, ci->index_block);

	/* Calculate required blocks for file (size > 128 bytes) */
	nr_allocs = DIV_ROUND_UP(new_size, OUICHEFS_BLOCK_SIZE);

	/* Check if we have enough free blocks */
	if (nr_allocs > inode->i_blocks - 1) {
		/* We need more blocks */
		uint32_t blocks_needed = nr_allocs - (inode->i_blocks - 1);
		if (blocks_needed > sbi->nr_free_blocks) {
			pr_err("Not enough free blocks: %u needed, %u available\n",
			       blocks_needed, sbi->nr_free_blocks);
			return -ENOSPC;
		}
	}

	/* Read index block from disk */
	if (ci->index_block == 0) {
		pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
		dump_stack();
	}
	bh_index = sb_bread(sb, ci->index_block);
	if (!bh_index) {
		ret = -EIO;
		pr_err("Failed to read index block %u\n", ci->index_block);
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
			pr_err("Block index %llu exceeds maximum (%d)\n",
			       block_idx, OUICHEFS_BLOCK_SIZE >> 2);
			goto out;
		}

		/* Get or allocate physical block */
		physical_block = le32_to_cpu(index->blocks[block_idx]);
		if (physical_block == 0) {
			/* Allocate new block */
			physical_block = get_free_block(sbi);

			if (!physical_block) {
				ret = -ENOSPC;
				pr_err("Failed to allocate physical block\n");
				goto out;
			}
			index->blocks[block_idx] = cpu_to_le32(physical_block);
			mark_buffer_dirty(bh_index);
		}

		/* Read the block (we might be doing partial write) */
		if (physical_block == 0) {
			pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
			dump_stack();
		}
		bh_data = sb_bread(sb, physical_block);
		if (!bh_data) {
			ret = -EIO;
			pr_err("Failed to read data block %u\n",
			       physical_block);
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
			pr_err("Failed to copy data from iter\n");
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

	return ret;
}

static uint32_t get_consequitive_free_slices(struct buffer_head **bh_data,
					     struct ouichefs_inode_info *ci,
					     loff_t file_size)
{
	/* Check how many consequtive slices we need and see if we have that many in the current block */
	uint32_t slice_to_write = 0;
	if (file_size > OUICHEFS_BLOCK_SIZE) {
		pr_err("File size %lld exceeds maximum allowed size %d\n",
		       file_size, OUICHEFS_BLOCK_SIZE);
		return 0; // No valid slice to write
	}

	uint32_t bitmap = *(uint32_t *)(*bh_data)->b_data;
	uint32_t num_slices_needed =
		DIV_ROUND_UP(file_size, OUICHEFS_SLICE_SIZE);
	uint32_t mask = ((1U << num_slices_needed) - 1) << 1;

	for (int i = 1; i <= OUICHEFS_BITMAP_SIZE_BITS - (num_slices_needed);
	     i++) {
		if ((bitmap | mask) == bitmap) {
			slice_to_write = i;
			*(uint32_t *)(*bh_data)->b_data &= ~mask;
			break;
		}
		mask <<= 1;
	}

	if (slice_to_write != 0) {
		ci->num_slices = (uint16_t)num_slices_needed;
		mark_inode_dirty(&ci->vfs_inode);
	}
	return slice_to_write;
}

static ssize_t write_small_file(struct inode *inode,
				struct ouichefs_inode_info *ci,
				struct super_block *sb,
				struct ouichefs_sb_info *sbi,
				struct kiocb *iocb, struct iov_iter *from)
{
	struct buffer_head *bh_index = NULL, *bh_data = NULL;
	size_t count = iov_iter_count(from);
	loff_t pos = iocb->ki_pos;
	if (iocb->ki_flags & IOCB_APPEND) {
		pos = inode->i_size;
		pr_info("IOCB_APPEND flag set, pos set to inode->i_size: %lld\n",
			inode->i_size);
	}
	ssize_t ret = 0;
	uint32_t block_to_write = 0;
	uint32_t slice_to_write = 0;
	loff_t old_size = inode->i_size;
	loff_t new_size = max((loff_t)(pos + count), old_size);

	uint32_t old_num_slices = DIV_ROUND_UP(old_size, OUICHEFS_SLICE_SIZE);
	uint32_t new_num_slices = DIV_ROUND_UP(new_size, OUICHEFS_SLICE_SIZE);

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

		} else {
			/* There already is a sliced block, read it. */
			block_to_write = sbi->s_free_sliced_blocks;
			if (block_to_write == 0) {
				pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
				dump_stack();
			}
			bh_data = sb_bread(sb, block_to_write);
			if (!bh_data) {
				pr_err("Failed to read sliced block\n");
				ret = -EIO;
				goto out;
			}
		}

		/* Check how many consequtive slices we need and see if we have that many in the current block */
		slice_to_write =
			get_consequitive_free_slices(&bh_data, ci, new_size);

		/* slice_to_write is 0 if we did not find a place to write our file in the block (we assume that the first bit is never free) */
		while (slice_to_write == 0) {
			/* Try fetch reference to next sliced block */
			block_to_write = OUICHEFS_SLICED_BLOCK_SB_NEXT(bh_data);

			/* Store reference to the current block as previous */
			bh_index = bh_data;
			bh_data = NULL;

			if (block_to_write == 0) {
				/* No next sliced block, allocate a new one */
				ssize_t free_block =
					allocate_and_init_slice_block(sb, sbi,
								      &bh_data);
				if (free_block <= 0) {
					ret = free_block;
					pr_info("Failed to allocate new sliced block: %lx\n",
						ret);
					goto out;
				}

				block_to_write = (uint32_t)free_block;

				/* Set pointer of previous block to this block */
				OUICHEFS_SLICED_BLOCK_SB_SET_NEXT(bh_index,
								  free_block);
				mark_buffer_dirty(bh_index);
				sync_dirty_buffer(bh_index);

				/* Release the previous block - we're done with it */
				brelse(bh_index);
				bh_index = NULL;
			} else {
				/* Read the next block */
				if (block_to_write == 0) {
					pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
					dump_stack();
				}
				bh_data = sb_bread(sb, block_to_write);
				if (!bh_data) {
					brelse(bh_index);
					ret = -EIO;
					goto out;
				}

				/* Release the previous block - we're done with it */
				brelse(bh_index);
				bh_index = NULL;
			}

			/* Check if this block has any empty slices */
			slice_to_write = get_consequitive_free_slices(
				&bh_data, ci, new_size);
		}
	} else {
		pr_info("This is a small file that has already been added to a sliced block.\n");
		uint32_t old_bno = OUICHEFS_SMALL_FILE_GET_BNO(ci);
		uint32_t old_slice_no = OUICHEFS_SMALL_FILE_GET_SLICE(ci);
		uint32_t old_index_block = ci->index_block;

		if (ci->num_slices == 0) {
			pr_err("num_slices is 0, this should never be the case here!\n");
			ret = -EIO;
			goto out;
		}
		old_num_slices = ci->num_slices;

		pr_info("old_bno: %u, old_slice_no: %u\n", old_bno,
			old_slice_no);

		if (old_bno == 0 || old_slice_no == 0) {
			pr_err("bno or slice_no is 0, this should not happen!\n");
			ret = -EIO;
			goto out;
		}

		if (old_bno == 0) {
			pr_err("CRITICAL: Attempted to access block 0 (superblock) as data block!\n");
			dump_stack();
		}
		bh_data = sb_bread(sb, old_bno);
		if (!bh_data) {
			pr_err("Failed to read sliced block %u\n",
			       block_to_write);
			ret = -EIO;
			goto out;
		}

		if (old_num_slices == new_num_slices) {
			pr_info("unchanged amount of slices, just writing the file");
			block_to_write = old_bno;
			slice_to_write = old_slice_no;
		} else {
			ci->index_block = 0;
			inode->i_size = 0;

			struct iov_iter *new_iter = from;
			struct kiocb *new_iocb = iocb;
			// uint32_t num_slices = ci->num_slices;

			if (pos == 0) {
				pr_info("pos is 0, we can ignore previous content");
				ret = write_small_file(inode, ci, sb, sbi,
						       new_iocb, new_iter);
			} else {
				/* Allocate buffer for the entire new file content */
				char *combined_buf =
					kmalloc(new_size, GFP_KERNEL);
				if (!combined_buf) {
					ret = -ENOMEM;
					goto out;
				}

				/* Copy existing file data to the beginning of the buffer */
				memcpy(combined_buf,
				       bh_data->b_data +
					       old_slice_no *
						       OUICHEFS_SLICE_SIZE,
				       old_size);

				/* Zero-fill any gap between old data and write position */
				if (pos > old_size) {
					memset(combined_buf + old_size, 0,
					       pos - old_size);
				}

				/* Copy new data from user space into the buffer */
				size_t copied = copy_from_iter(
					combined_buf + pos, count, from);
				if (copied != count) {
					ret = -EFAULT;
					goto out;
				}

				struct kvec *kv = kmalloc(sizeof(struct kvec),
							  GFP_KERNEL);
				if (!kv) {
					ret = -ENOMEM;
					goto out;
				}
				kv->iov_base = combined_buf;
				kv->iov_len = new_size;

				/* Create iterator that contains the complete new file */
				iov_iter_kvec(new_iter, READ, kv, 1, new_size);
				new_iter->data_source = 1;

				/* Reset position to 0 and remove append flag - we're writing entire file */
				new_iocb->ki_pos = 0;
				new_iocb->ki_flags &= ~IOCB_APPEND;

				/* Write the entire new file content */
				ret = write_small_file(inode, ci, sb, sbi,
						       new_iocb, new_iter);

				kfree(kv);
				kfree(combined_buf);
			}

			if (ret > 0) {
				/* Write was successful, update inode and delete old slices */
				iocb->ki_pos = pos + count;
				ret = count;
				delete_slice(sb, sbi, old_bno, old_slice_no,
					     old_num_slices);

			} else {
				/* Write failed, restore old inode */
				pr_err("Failed to write small file: %zd\n",
				       ret);

				ci->index_block = old_index_block;
				inode->i_size = old_size;
				iocb->ki_pos = pos;
			}
			return ret;
		}
	}

	if (!bh_data) {
		/* We should have found a slice by now. If bh_data is null it means we have not. */
		pr_err("bh_data is NULL, this should not happen!\n");
		ret = -EIO;
		goto out;
	}

	pr_info("block_to_write: %d, slice to write: %d, pos: %llu\n",
		block_to_write, slice_to_write, pos);

	/* Copy data from user space */
	if (copy_from_iter(bh_data->b_data +
				   slice_to_write * OUICHEFS_SLICE_SIZE + pos,
			   count, from) != count) {
		brelse(bh_data);
		ret = -EFAULT;
		goto out;
	}

	pr_info("BEFORE sbi->nr_used_slices: %u\n", sbi->nr_used_slices);
	sbi->nr_used_slices += new_num_slices;
	pr_info("AFTER sbi->nr_used_slices: %u\n", sbi->nr_used_slices);

	/* Mark buffer dirty and sync */
	mark_buffer_dirty(bh_data);
	sync_dirty_buffer(bh_data);
	brelse(bh_data);
	bh_data = NULL;
	ret = count;

	/* Update inode metadata */
	inode->i_size = new_size;
	inode->i_mtime = inode->i_ctime = current_time(inode);

	ci->index_block = (block_to_write << 5) + slice_to_write;
	pr_info("ci->index_block: %u\n\n", ci->index_block);
	mark_inode_dirty(inode);

	/* Update file position */
	iocb->ki_pos = pos;

	goto out;
out:
	// pr_info("returning : %lx\n", ret);
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
