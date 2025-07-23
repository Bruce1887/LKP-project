#include <asm-generic/errno-base.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/file.h>
#include <linux/buffer_head.h>
#include "ouichefs.h"
#include "ioctl.h"

static int major;

static long ouichefs_ioctl(struct file *dev_file, unsigned int cmd, unsigned long arg)
{
	if (cmd == OUICHEFS_DEBUG_IOCTL) {
		struct ouichefs_debug_ioctl value;
		if (copy_from_user(&value ,(struct ouichefs_debug_ioctl *) arg, sizeof(struct ouichefs_debug_ioctl))) {
				pr_err("Failed to read value\n");
				return -EFAULT;
		}

		if (!value.data) {
			pr_err("data should not be null");
			return -EINVAL;
		}

		struct file *file = fget(value.target_file);
		if (!file) {
			pr_err("File not found");
			return -ENOENT;
		}
		struct inode *inode = file_inode(file);
		struct super_block *sb = inode->i_sb;
		struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
		struct buffer_head *bh_data;

		// TODO: check if file is from ouichefs

		if (inode->i_blocks != 0) {
			pr_err("Not a sliced file");
			return -EINVAL;
		}

		uint32_t bno = OUICHEFS_SMALL_FILE_GET_BNO(ci);
		uint32_t slice_no = OUICHEFS_SMALL_FILE_GET_SLICE(ci);

		pr_info("requested file is in slice_no: %u\n", slice_no);

		bh_data = sb_bread(sb, bno);
		if (!bh_data) {
			pr_err("Failed to read sliced block %u\n", bno);
			return -EIO;
		}

		if (copy_to_user(value.data, bh_data->b_data, OUICHEFS_SLICE_SIZE * 32)) {
				pr_err("Failed to write value\n");
				return -EFAULT;
		}

		brelse(bh_data);
		return 0;
	} else {
		pr_info("Not a command\n");

	return 0;
	}
}

static int ouichefs_open(struct inode *inode, struct file *file)
{
        pr_info("ouichefs_open\n");
        return 0;
}

static int ouichefs_release(struct inode *inode, struct file *file)
{
        pr_info("ouichefs_release\n");
        return 0;
}

static struct file_operations fops =
{
        .owner          = THIS_MODULE,
		.open			= ouichefs_open,
		.release		= ouichefs_release,
        .unlocked_ioctl = ouichefs_ioctl,
};

void ouichefs_register_device(void)
{
	major = register_chrdev(0, "ouichefs", &fops);
	pr_info("major: %d", major);
}

void ouichefs_unregister_device(void)
{
	unregister_chrdev(major, "ouichefs");
}

