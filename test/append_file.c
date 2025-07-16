#include <stdio.h>
#include "util.h"
#include "error.h"
#include "tests.h"

#define A_EMPTY_1_NAME "aempty1.txt"

int append_empty_to_small_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(A_EMPTY_1_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(A_EMPTY_1_NAME), "a");
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

	file = fopen(OUICHEFS_FILE_NAME(A_EMPTY_1_NAME), "r");
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

#define A_EMPTY_2_NAME "aempty2.txt"

int append_empty_to_big_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(A_EMPTY_2_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(A_EMPTY_2_NAME), "a");
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

	file = fopen(OUICHEFS_FILE_NAME(A_EMPTY_2_NAME), "r");
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

#define A_SMALL_1_NAME "asmall1.txt"

int append_small_to_small_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(A_SMALL_1_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(A_SMALL_1_NAME), "a");
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

	file = fopen(OUICHEFS_FILE_NAME(A_SMALL_1_NAME), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, PAYLOAD100);
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}

#define A_SMALL_2_NAME "asmall2.txt"

int append_small_to_big_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(A_SMALL_2_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(A_SMALL_2_NAME), "a");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD200);
	if (ret != 200) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(A_SMALL_2_NAME), "r");
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

#define A_BIG_NAME "abig.txt"

int append_big_to_big_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(A_BIG_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(A_BIG_NAME), "a");
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

	file = fopen(OUICHEFS_FILE_NAME(A_BIG_NAME), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, PAYLOAD500);
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}
