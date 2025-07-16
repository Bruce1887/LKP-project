/* SPDX-License-Identifier: GPL-2.0 */
/*
 * ouiche_fs - a simple educational filesystem for Linux
 *
 * Copyright (C) 2018 Redha Gouicem <redha.gouicem@lip6.fr>
 */
#ifndef _OUICHEFS_H
#define _OUICHEFS_H

#include <linux/fs.h>

#define OUICHEFS_MAGIC 0x48434957

#define OUICHEFS_SB_BLOCK_NR 0

#define OUICHEFS_BLOCK_SIZE (1 << 12) /* 4 KiB */
#define OUICHEFS_MAX_FILESIZE (1 << 22) /* 4 MiB */
#define OUICHEFS_FILENAME_LEN 28
#define OUICHEFS_MAX_SUBFILES 128
#define OUICHEFS_SLICE_SIZE 128

/*
 * ouiche_fs partition layout
 *
 * +---------------+
 * |  superblock   |  1 block
 * +---------------+
 * |  inode store  |  sb->nr_istore_blocks blocks
 * +---------------+
 * | ifree bitmap  |  sb->nr_ifree_blocks blocks
 * +---------------+
 * | bfree bitmap  |  sb->nr_bfree_blocks blocks
 * +---------------+
 * |	data	   |
 * |	  blocks   |  rest of the blocks
 * +---------------+
 *
 */

struct ouichefs_inode {
	__le32 i_mode; /* File mode */
	__le32 i_uid; /* Owner id */
	__le32 i_gid; /* Group id */
	__le32 i_size; /* Size in bytes */
	__le32 i_ctime; /* Inode change time (sec)*/
	__le64 i_nctime; /* Inode change time (nsec) */
	__le32 i_atime; /* Access time (sec) */
	__le64 i_natime; /* Access time (nsec) */
	__le32 i_mtime; /* Modification time (sec) */
	__le64 i_nmtime; /* Modification time (nsec) */
	__le32 i_blocks; /* Block count */
	__le32 i_nlink; /* Hard links count */
	__le32 index_block; /* Block with list of blocks for this file */
};

struct ouichefs_inode_info {
	uint32_t index_block;
	struct inode vfs_inode;
};

#define OUICHEFS_INODES_PER_BLOCK \
	(OUICHEFS_BLOCK_SIZE / sizeof(struct ouichefs_inode))

struct ouichefs_sb_info {
	uint32_t magic; /* Magic number */

	uint32_t nr_blocks; /* Total number of blocks (incl sb & inodes) */
	uint32_t nr_inodes; /* Total number of inodes */

	uint32_t nr_istore_blocks; /* Number of inode store blocks */
	uint32_t nr_ifree_blocks; /* Number of inode free bitmap blocks */
	uint32_t nr_bfree_blocks; /* Number of block free bitmap blocks */

	uint32_t nr_free_inodes; /* Number of free inodes */
	uint32_t nr_free_blocks; /* Number of free blocks */

	unsigned long *ifree_bitmap; /* In-memory free inodes bitmap */
	unsigned long *bfree_bitmap; /* In-memory free blocks bitmap */

	uint32_t s_free_sliced_blocks; /* Number of the first free sliced block (0 if there is none) */

	struct kobject s_kobj; /* sysfs kobject */
	struct super_block *
		s_sb; /* Containing super_block reference  TODO: is this okay? */
};

struct ouichefs_file_index_block {
	__le32 blocks[OUICHEFS_BLOCK_SIZE >> 2];
};

struct ouichefs_dir_block {
	struct ouichefs_file {
		__le32 inode;
		char filename[OUICHEFS_FILENAME_LEN];
	} files[OUICHEFS_MAX_SUBFILES];
};

#define OUICHEFS_BITMAP_SIZE_BITS (sizeof(uint32_t) * 8)
#define OUICHEFS_BITMAP_ALL_FREE 4294967294 /* unsigned 32 bit number. 31 '1' bits and 1 '0' */

#define OUICHEFS_BITMAP_IS_ALL_FREE(bh) \
	(OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh) == OUICHEFS_BITMAP_ALL_FREE)

/* Finds the first set bit (1) out of the first 32 bits and clears it (0). 
   Bit 0 is the first bit and is always 0, and can this be used to indicate 
   that no free bit was found. */
#define OUICHEFS_GET_FIRST_FREE_BIT(bh)                 \
	get_first_free_bit((unsigned long *)bh->b_data, \
			   (OUICHEFS_BITMAP_SIZE_BITS))

/* Sliced block superblock getters/setters */
#define OUICHEFS_SLICED_BLOCK_SB_BITMAP(bh) (*((uint32_t *)((bh)->b_data)))

#define OUICHEFS_SLICED_BLOCK_SB_SET_BITMAP(bh, val) \
	(*((uint32_t *)((bh)->b_data)) = (val))

#define OUICHEFS_SLICED_BLOCK_SB_NEXT(bh) (*((uint32_t *)((bh)->b_data + 4)))

#define OUICHEFS_SLICED_BLOCK_SB_SET_NEXT(bh, val) \
	(*((uint32_t *)((bh)->b_data + 4)) = (val))

/* small file index_block getters */
#define OUICHEFS_SMALL_FILE_GET_BNO(inode) \
	((inode->index_block) >> 5) /* Get the number of the block (27 bits)*/

#define OUICHEFS_SMALL_FILE_GET_SLICE(inode) \
	((inode->index_block) &              \
	 0b11111) /* Get the slice in the block (5 bits to identify 32 slices)*/

/* superblock functions */
int ouichefs_fill_super(struct super_block *sb, void *data, int silent);

/* inode functions */
int ouichefs_init_inode_cache(void);
void ouichefs_destroy_inode_cache(void);
struct inode *ouichefs_iget(struct super_block *sb, unsigned long ino);

/* file functions */
extern const struct file_operations ouichefs_file_ops;
extern const struct file_operations ouichefs_dir_ops;
extern const struct address_space_operations ouichefs_aops;

/* sysfs */
extern int ouichefs_register_sysfs(struct super_block *sb);
extern void ouichefs_unregister_sysfs(struct super_block *sb);
extern int __init ouichefs_init_sysfs(void);
extern void ouichefs_exit_sysfs(void);

/* ioctl */
extern void ouichefs_register_device(void);
extern void ouichefs_unregister_device(void);

/* Getters for superbock and inode */
#define OUICHEFS_SB(sb) (sb->s_fs_info)
#define OUICHEFS_INODE(inode) \
	(container_of(inode, struct ouichefs_inode_info, vfs_inode))

/**
 * @brief Deletes a slice and clears data in sliced block. 
 * This function also checks if the sliced block is now completely vacant
 * and frees it if so. It updates the ouichefs super block info structure
 * to reflect the new state of the sliced block and the filesystem.
 * Does not free the inode info structure, only the slice and potentially the sliced block.
 * It does however alter the inode info structure by resetting the index_block field to 0.
 * 
 * @param ci The ouichefs inode info structure containing the small file's metadata.
 * @param sb The super block of the ouichefs filesystem.
 * @param sbi The ouichefs super block info structure containing filesystem metadata.
 * @return ssize_t Returns 0 on success, or a negative error code on failure.
 */
ssize_t delete_slice_and_clear_inode(struct ouichefs_inode_info *ci, struct super_block *sb,
		     struct ouichefs_sb_info *sbi);
#endif /* _OUICHEFS_H */
