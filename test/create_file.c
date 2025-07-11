#include <stdio.h>
#include "util.h"
#include "error.h"
#include "tests.h"

#define C_EMPTY_NAME "cempty.txt"

int create_empty_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(C_EMPTY_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(C_EMPTY_NAME), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, "");
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}

#define C_SMALL_NAME "csmall.txt"

int create_small_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(C_SMALL_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD50);
	if (ret != 50) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(C_SMALL_NAME), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, PAYLOAD50);
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}

#define C_BIG_NAME "cbig.txt"

int create_big_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(C_BIG_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD250);
	if (ret != 250) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(C_BIG_NAME), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, PAYLOAD250);
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}
