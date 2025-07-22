#include <stdio.h>
#include "util.h"
#include "error.h"
#include "tests.h"

#define T_SMALL_1_NAME "tsmall1.txt"

int truncate_small_to_empty_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(T_SMALL_1_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(T_SMALL_1_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, "");
	if (ret != 0) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(T_SMALL_1_NAME), "r");
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

#define T_SMALL_2_NAME "tsmall2.txt"

int truncate_small_to_small_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(T_SMALL_2_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(T_SMALL_2_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD20);
	if (ret != 20) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(T_SMALL_2_NAME), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, PAYLOAD20);
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}

#define T_BIG_1_NAME "tbig1.txt"

int truncate_big_to_empty_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(T_BIG_1_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(T_BIG_1_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, "");
	if (ret != 0) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(T_BIG_1_NAME), "r");
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


#define T_BIG_2_NAME "tbig2.txt"

int truncate_big_to_small_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(T_BIG_2_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(T_BIG_2_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(T_BIG_2_NAME), "r");
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

#define T_BIG_3_NAME "tbig3.txt"

int truncate_big_to_big_file(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(T_BIG_3_NAME), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD500);
	if (ret != 500) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(T_BIG_3_NAME), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(T_BIG_3_NAME), "r");
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
