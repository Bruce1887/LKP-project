#include "util.h"
#include "tests.h"

int main(void)
{
	RUN_AND_CHECK(create_empty_file);
	RUN_AND_CHECK(create_small_file);
	RUN_AND_CHECK(create_big_file);

	RUN_AND_CHECK(append_empty_to_small_file);
	RUN_AND_CHECK(append_empty_to_big_file);
	RUN_AND_CHECK(append_small_to_small_file);
	RUN_AND_CHECK(append_small_to_big_file);
	RUN_AND_CHECK(append_big_to_big_file);

	RUN_AND_CHECK(truncate_small_to_empty_file);
	RUN_AND_CHECK(truncate_small_to_small_file);
	RUN_AND_CHECK(truncate_big_to_empty_file);
	RUN_AND_CHECK(truncate_big_to_small_file);
	RUN_AND_CHECK(truncate_big_to_big_file);

	RUN_AND_CHECK(slice_expand_1_2);
	RUN_AND_CHECK(slice_expand_next_block);
	RUN_AND_CHECK(slice_truncate_2_1);
}

