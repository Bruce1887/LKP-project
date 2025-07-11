#include "util.h"
#include "tests.h"

int main(void)
{
	RUN_AND_CHECK(create_empty_file);
	RUN_AND_CHECK(create_small_file);
	RUN_AND_CHECK(create_big_file);
}

