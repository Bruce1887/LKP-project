#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include "ouichefs.h"


#define SBI_FROM_KOBJ(kobj) \
	(container_of(kobj, struct ouichefs_sb_info, s_kobj))

#define OUICHEFS_ATTR(name) \
	static struct kobj_attribute name##_attribute = \
	__ATTR(name, 0400, name##_show, NULL);


static struct kobject *ouichefs_root;

static loff_t total_data_size(struct ouichefs_sb_info *sbi)
{
	loff_t size = 0;

	// TODO: lock?
	for (int i = 0; i < sbi->nr_blocks; i++) {
		struct inode *inode = ouichefs_iget(sbi->s_sb, i);
		if (!inode) {
			pr_err("%s: failed to read inode\n", __func__);
		}

		size += inode->i_size;
	}

	return size;
}

static loff_t total_used_size(struct ouichefs_sb_info *sbi)
{
	return sbi->nr_istore_blocks * BLOCK_SIZE;
}

static ssize_t free_blocks_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u", SBI_FROM_KOBJ(kobj)->nr_free_blocks);
}

static ssize_t used_blocks_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	return snprintf(buf, PAGE_SIZE, "%u", sbi->nr_blocks - sbi->nr_free_blocks);
}

static ssize_t sliced_blocks_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "todo");
}

static ssize_t total_free_slices_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "todo");
}

static ssize_t files_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u", SBI_FROM_KOBJ(kobj)->nr_inodes);
}

static ssize_t small_files_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "todo");
}

static ssize_t total_data_size_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu", total_data_size(SBI_FROM_KOBJ(kobj)));
}

static ssize_t total_used_size_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu", total_used_size(SBI_FROM_KOBJ(kobj)));
}

static ssize_t efficiency_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
	struct ouichefs_sb_info *sbi = SBI_FROM_KOBJ(kobj);
	return snprintf(buf, PAGE_SIZE, "%llu%%", ((total_data_size(sbi) * 100) / total_used_size(sbi)));
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
	&free_blocks_attribute.attr,
	&used_blocks_attribute.attr,
	&sliced_blocks_attribute.attr,
	&total_free_slices_attribute.attr,
	&files_attribute.attr,
	&small_files_attribute.attr,
	&total_data_size_attribute.attr,
	&total_used_size_attribute.attr,
	&efficiency_attribute.attr,
	NULL,
};

ATTRIBUTE_GROUPS(ouichefs);

static const struct kobj_type ouichefs_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_groups = ouichefs_groups
};

int ouichefs_register_sysfs(struct super_block *sb)
{
	int error;
	struct ouichefs_sb_info *sbi = OUICHEFS_SB(sb);

	error = kobject_init_and_add(&sbi->s_kobj, &ouichefs_ktype, ouichefs_root, "%s", sb->s_id);

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
