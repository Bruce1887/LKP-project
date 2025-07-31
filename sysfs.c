#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include "ouichefs.h"

#define SBI_FROM_KOBJ(kobj) \
	(container_of(kobj, struct ouichefs_sb_info, s_kobj))

#define OUICHEFS_ATTR(name)                             \
	static struct kobj_attribute name##_attribute = \
		__ATTR(name, 0400, name##_show, NULL);

static struct kobject *ouichefs_root;

static loff_t total_data_size(struct ouichefs_sb_info *sbi)
{
	loff_t size = 0;
	uint32_t inodes = sbi->nr_inodes - sbi->nr_free_inodes;

	for (int i = 0; i < inodes; i++) {
		struct inode *inode = ouichefs_iget(sbi->s_sb, i);
		if (!inode) {
			pr_err("%s: failed to read inode\n", __func__);
			continue;
		}

		size += inode->i_size;
		iput(inode);
	}

	return size;
}

static uint32_t total_file_count(struct ouichefs_sb_info *sbi)
{
	uint32_t count = 0;
	uint32_t inodes = sbi->nr_inodes - sbi->nr_free_inodes;

	for (int i = 0; i < inodes; i++) {
		struct inode *inode = ouichefs_iget(sbi->s_sb, i);
		if (!inode) {
			pr_err("%s: failed to read inode\n", __func__);
			continue;
		}

		struct ouichefs_inode_info *ci = OUICHEFS_INODE(inode);
		if (!ci) {
			pr_err("%s: failed to read ouichefs inode\n", __func__);
			iput(inode);
			continue;
		}

		if (S_ISDIR(inode->i_mode)) {
			iput(inode);
			continue;
		}

		count++;
		iput(inode);
	}

	return count;
}

static uint32_t total_small_file_count(struct ouichefs_sb_info *sbi)
{
	uint32_t count = 0;
	uint32_t inodes = sbi->nr_inodes - sbi->nr_free_inodes;

	pr_info("%s: inodes: %u, sbi->nr_inodes %u, free inodes: %u\n",
		__func__, inodes, sbi->nr_inodes, sbi->nr_free_inodes);

	for (int i = 1; i < inodes + 1; i++) { /* Skip first invalid inode 0*/
		struct inode *inode = ouichefs_iget(sbi->s_sb, i);
		if (!inode) {
			pr_err("%s: failed to read inode\n", __func__);
			iput(inode);
			continue;
		}
		pr_info("inode %lu: i_blocks: %llu, index_block: %u, is_dir: %u\n",
			inode->i_ino, inode->i_blocks,
			OUICHEFS_INODE(inode)->index_block,
			S_ISDIR(inode->i_mode));

		if (S_ISDIR(inode->i_mode)) {
			iput(inode);
			continue;
		}

		if (inode->i_blocks == 0) {
			count++;
		}
		iput(inode);
	}

	return count;
}

static loff_t total_used_size(struct ouichefs_sb_info *sbi)
{
	return (sbi->nr_blocks - sbi->nr_free_blocks) * BLOCK_SIZE;
}

static ssize_t free_blocks_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u",
			SBI_FROM_KOBJ(kobj)->nr_free_blocks);
}

static ssize_t used_blocks_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	pr_info("%s: sbi->nr_blocks: %u, sbi->nr_free_blocks: %u, sbi->nr_blocks - sbi->nr_free_blocks: %u\n",
		__func__, sbi->nr_blocks, sbi->nr_free_blocks,
		sbi->nr_blocks - sbi->nr_free_blocks);
	return snprintf(buf, PAGE_SIZE, "%u",
			sbi->nr_blocks - sbi->nr_free_blocks);
}

static ssize_t sliced_blocks_show(struct kobject *kobj,
				  struct kobj_attribute *attr, char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	return snprintf(buf, PAGE_SIZE, "%u", sbi->nr_sliced_blocks);
}

static ssize_t total_free_slices_show(struct kobject *kobj,
				      struct kobj_attribute *attr, char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	if (sbi->s_free_sliced_blocks == 0)
		return snprintf(buf, PAGE_SIZE, "0");

	pr_info("%s: sbi->nr_sliced_blocks: %u, sbi->nr_used_slices: %u\n",
		__func__, sbi->nr_sliced_blocks, sbi->nr_used_slices);

	return snprintf(buf, PAGE_SIZE, "%u",
			sbi->nr_sliced_blocks *
					OUICHEFS_SLICES_PER_SLICED_BLOCK -
				sbi->nr_used_slices);
}

static ssize_t files_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	return snprintf(buf, PAGE_SIZE, "%u", total_file_count(sbi));
}

static ssize_t small_files_show(struct kobject *kobj,
				struct kobj_attribute *attr, char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	return snprintf(buf, PAGE_SIZE, "%u", total_small_file_count(sbi));
}

static ssize_t total_data_size_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu",
			total_data_size(SBI_FROM_KOBJ(kobj)));
}

static ssize_t total_used_size_show(struct kobject *kobj,
				    struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu",
			total_used_size(SBI_FROM_KOBJ(kobj)));
}

static ssize_t efficiency_show(struct kobject *kobj,
			       struct kobj_attribute *attr, char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	return snprintf(buf, PAGE_SIZE, "%llu%%",
			((total_data_size(sbi) * 100) / total_used_size(sbi)));
}

OUICHEFS_ATTR(free_blocks);
OUICHEFS_ATTR(used_blocks);
OUICHEFS_ATTR(sliced_blocks);
OUICHEFS_ATTR(total_free_slices);
OUICHEFS_ATTR(files);
OUICHEFS_ATTR(small_files);
OUICHEFS_ATTR(total_data_size);
OUICHEFS_ATTR(total_used_size);
OUICHEFS_ATTR(efficiency);

static struct attribute *ouichefs_attrs[] = {
	&free_blocks_attribute.attr,	 &used_blocks_attribute.attr,
	&sliced_blocks_attribute.attr,	 &total_free_slices_attribute.attr,
	&files_attribute.attr,		 &small_files_attribute.attr,
	&total_data_size_attribute.attr, &total_used_size_attribute.attr,
	&efficiency_attribute.attr,	 NULL,
};

ATTRIBUTE_GROUPS(ouichefs);

static const struct kobj_type ouichefs_ktype = { .sysfs_ops = &kobj_sysfs_ops,
						 .default_groups =
							 ouichefs_groups };

int ouichefs_register_sysfs(struct super_block *sb)
{
	int error;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	error = kobject_init_and_add(&sbi->s_kobj, &ouichefs_ktype,
				     ouichefs_root, "%s", sb->s_id);

	if (error) {
		return -ENOMEM;
	}

	return 0;
}

void ouichefs_unregister_sysfs(struct super_block *sb)
{
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);
	kobject_put(&sbi->s_kobj);
}

int __init ouichefs_init_sysfs(void)
{
	ouichefs_root = kobject_create_and_add("ouichefs", fs_kobj);
	if (!ouichefs_root)
		return -ENOMEM;

	return 0;
}

void ouichefs_exit_sysfs(void)
{
	kobject_put(ouichefs_root);
	ouichefs_root = NULL;
}
