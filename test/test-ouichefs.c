#include "util.h"
#include "tests.h"
#include <stdio.h>

int run_and_check(int (* test_fn)(void), const char* test_fn_name) {
	int ret = test_fn();
	if (ret) {
		fprintf(stderr, "Error: %s failed with code %d\n", test_fn_name, ret);
	}
	else {
		fprintf(stdout, "Success: %s\n", test_fn_name);
	}

	return ret != 0;
}

int main(void)
{
	int failed_count = 0;

	failed_count += run_and_check(create_empty_file, NAMEOF(create_empty_file));
	failed_count += run_and_check(create_small_file, NAMEOF(create_small_file));
	failed_count += run_and_check(create_big_file, NAMEOF(create_big_file));

	failed_count += run_and_check(remove_empty_file, NAMEOF(remove_empty_file));
	failed_count += run_and_check(remove_small_file, NAMEOF(remove_small_file));
	failed_count += run_and_check(remove_big_file, NAMEOF(remove_big_file));

	failed_count += run_and_check(append_empty_to_small_file, NAMEOF(append_empty_to_small_file));
	failed_count += run_and_check(append_empty_to_big_file, NAMEOF(append_empty_to_big_file));
	failed_count += run_and_check(append_small_to_small_file, NAMEOF(append_small_to_small_file));
	failed_count += run_and_check(append_small_to_big_file, NAMEOF(append_small_to_big_file));
	failed_count += run_and_check(append_big_to_big_file, NAMEOF(append_big_to_big_file));

	failed_count += run_and_check(truncate_small_to_empty_file, NAMEOF(truncate_small_to_empty_file));
	failed_count += run_and_check(truncate_small_to_small_file, NAMEOF(truncate_small_to_small_file));
	failed_count += run_and_check(truncate_big_to_empty_file, NAMEOF(truncate_big_to_empty_file));
	failed_count += run_and_check(truncate_big_to_small_file, NAMEOF(truncate_big_to_small_file));
	failed_count += run_and_check(truncate_big_to_big_file, NAMEOF(truncate_big_to_big_file));

	failed_count += run_and_check(slice_expand_1_2, NAMEOF(slice_expand_1_2));
	failed_count += run_and_check(slice_expand_next_block, NAMEOF(slice_expand_next_block));
	failed_count += run_and_check(slice_truncate_2_1, NAMEOF(slice_truncate_2_1));

	if (failed_count) {
		fprintf(stderr, "Failed tests: %d\n", failed_count);
	} else {
		fprintf(stdout, "All tests successful\n");
	}
}

