#define OUICHEFS_DEBUG_IOCTL _IOR('o', 0, struct ouichefs_debug_ioctl)

struct ouichefs_debug_ioctl {
	int target_file;
	char *data;
};

