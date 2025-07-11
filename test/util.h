#include "error.h"
#include <stdio.h>

#define RUN_AND_CHECK(fn) \
	do { \
		int ret = fn(); \
		if (ret) { \
			fprintf(stderr, "Error: %s failed with code %d\n", #fn, ret); \
		} \
	} while(0);

#define OUICHEFS_BASE_DIR "/mnt/ouiche/"
#define OUICHEFS_FILE_NAME(name) OUICHEFS_BASE_DIR name

#define PAYLOAD10 "aaaaaaaaaa"
#define PAYLOAD20 PAYLOAD10 PAYLOAD10
#define PAYLOAD50 PAYLOAD20 PAYLOAD20 PAYLOAD10
#define PAYLOAD100 PAYLOAD50 PAYLOAD50
#define PAYLOAD250 PAYLOAD100 PAYLOAD100 PAYLOAD50

int read_and_cmp_content(FILE *file, char *expected);
