#include "error.h"
#include <stdio.h>

#define RUN_AND_CHECK(fn) \
	do { \
		int ret = fn(); \
		if (ret) { \
			fprintf(stderr, "Error: %s failed with code %d\n", #fn, ret); \
		} \
		else { \
			fprintf(stdout, "Success: %s\n", #fn); \
		} \
	} while(0);

#define OUICHEFS_BASE_DIR "/mnt/ouiche/"
#define OUICHEFS_FILE_NAME(name) OUICHEFS_BASE_DIR name

#define PAYLOAD10 "aaaaaaaaaa"
#define PAYLOAD20 PAYLOAD10 PAYLOAD10
#define PAYLOAD50 PAYLOAD20 PAYLOAD20 PAYLOAD10
#define PAYLOAD100 PAYLOAD50 PAYLOAD50
#define PAYLOAD200 PAYLOAD100 PAYLOAD100
#define PAYLOAD250 PAYLOAD200 PAYLOAD50
#define PAYLOAD500 PAYLOAD250 PAYLOAD250
#define PAYLOAD1000 PAYLOAD500 PAYLOAD500
#define PAYLOAD2500 PAYLOAD1000 PAYLOAD1000 PAYLOAD500
#define PAYLOAD3000 PAYLOAD2500 PAYLOAD500

int read_and_cmp_content(FILE *file, char *expected);
