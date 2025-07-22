#include "../ioctl.h"
#include <cstdlib>
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

		FILE* dev_file = fopen("/dev/ouichefs", "r");
		int dev_fd = fileno(dev_file);

		printf("target_fd: %d, dev_fd: %d\n", target_fd, dev_fd);

		struct ouichefs_debug_ioctl *ouichefs_ioctl = malloc(sizeof(struct ouichefs_debug_ioctl));
		ouichefs_ioctl->target_file = target_fd;
		ouichefs_ioctl->data = malloc(128 * 32);

		ioctl(dev_fd, OUICHEFS_DEBUG_IOCTL, ouichefs_ioctl);

		for (int i = 0; i < 32; i++) {
			for (int j = 0; j < 128; j++) {
				printf("%02X", ouichefs_ioctl->data[j + i * 128]);
			}
			printf("\n");
		}

		free(ouichefs_ioctl->data);
		free(ouichefs_ioctl);

		fclose(target_file);
		fclose(dev_file);
	}

	return 0;
}
