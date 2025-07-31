#include <stdio.h>
#include "tests.h"
#include "util.h"
#include "error.h"

#define S_EXPAND_1_2 "s_expand_1_2.txt"

int slice_expand_1_2(void)
{
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(S_EXPAND_1_2), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD100);
	if (ret != 100) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(S_EXPAND_1_2), "a");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD100);
	if (ret != 100) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(S_EXPAND_1_2), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, PAYLOAD200);
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}

#define S_EXPAND_B_1 "s_expand_b_1.txt"
#define S_EXPAND_B_2 "s_expand_b_2.txt"

int slice_expand_next_block(void) {

	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(S_EXPAND_B_1), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD2500);
	if (ret != 2500) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(S_EXPAND_B_2), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(S_EXPAND_B_2), "a");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD2500);
	if (ret != 2500) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(S_EXPAND_B_2), "r");
	if (!file)
		return ERR_OPEN;

	ret = read_and_cmp_content(file, PAYLOAD3000);
	if (ret)
		return ret;

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	return 0;
}

#define S_TRUNCATE_2_1 "s_truncate_2_1.txt"

int slice_truncate_2_1(void) {
	int ret;
	FILE *file = fopen(OUICHEFS_FILE_NAME(S_TRUNCATE_2_1), "w");
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

	file = fopen(OUICHEFS_FILE_NAME(S_TRUNCATE_2_1), "w");
	if (!file)
		return ERR_CREATE;

	ret = fprintf(file, PAYLOAD100);
	if (ret != 100) {
		fprintf(stderr, "%s: fprintf returned %d", __func__, ret);
		return ERR_WRITE;
	}

	ret = fclose(file);
	if (ret)
		return ERR_CLOSE;

	file = fopen(OUICHEFS_FILE_NAME(S_TRUNCATE_2_1), "r");
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
