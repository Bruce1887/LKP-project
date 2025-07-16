#include "../ioctl.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/ioctl.h>

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr, "expected at least 1 command");
		exit(-1);
	}

	if (strcmp(argv[1], "ioctl") == 0) {
		if (argc != 3) {
			fprintf(stderr, "expected just 1 arguments for ioctl command");
			exit(-1);
		}
		FILE* target_file = fopen(argv[2], "r");
		int target_fd = fileno(target_file);

		FILE* dev_file = fopen("dev/ouichefs", "r");
		int dev_fd = fileno(dev_file);

		ioctl(dev_fd, OUICHEFS_DEBUG_IOCTL, &target_fd);

		fclose(target_file);
		fclose(dev_file);
	}

	return 0;
}
