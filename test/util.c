#include <stdio.h>
#include <string.h>

#include "util.h"

int read_and_cmp_content(FILE *file, char *expected)
{
	if (strnlen(expected, 4096) == 0) {
		int c = fgetc(file);
		if (c != EOF) {
			return ERR_CMP;
		}

		return 0;
	}

	const int buf_size = 4096;
	char buf[buf_size];
	char *ret = fgets(buf, buf_size, file);
	if (!ret)
		return ERR_READ;

	int result = strncmp(buf, expected, buf_size);
	if (result != 0)
		return ERR_CMP;

	return 0;
}
