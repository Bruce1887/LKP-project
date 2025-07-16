#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/file.h>
#include <linux/buffer_head.h>
#include "ouichefs.h"
#include "ioctl.h"

static int major;

static long ouichefs_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	if (cmd == OUICHEFS_DEBUG_IOCTL) {
		uint32_t value;
		if (copy_from_user(&value ,(struct ouichefs_debug_ioctl *) arg, sizeof(value)))
		{
				pr_err("Failed to read value\n");
				return -EFAULT;
		}
		struct file *file = fget(value);
		if (!file) {
			pr_err("File not found");
			return -ENOENT;
		}
		struct inode *inode = file_inode(file);
		struct super_block *sb = inode->i_sb;
		struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
		struct buffer_head *bh_data;

		if (inode->i_blocks != 0) {
			pr_err("Not a sliced file");
			return -EINVAL;
		}

		uint32_t bno = OUICHEFS_SMALL_FILE_GET_BNO(ci);

		bh_data = sb_bread(sb, bno);
		if (!bh_data) {
			pr_err("Failed to read sliced block %u\n", bno);
			return -EIO;
		}

		char tmp[128];
		for (int i = 0; i < 32; i++) {
				strncpy(tmp, bh_data->b_data + i * 128, 128);
				pr_info("line1: %s", tmp);
		}

		brelse(bh_data);
		return 0;
	} else {
		pr_info("Not a command\n");

	return 0;
	}
}

static struct file_operations fops =
{
        .owner          = THIS_MODULE,
        .unlocked_ioctl = ouichefs_ioctl,
};

void ouichefs_register_device(void)
{
	major = register_chrdev(0, "ouichefs", &fops);
}

void ouichefs_unregister_device(void)
{
	unregister_chrdev(major, "ouichefs");
}

