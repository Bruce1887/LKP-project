int create_empty_file(void);
int create_small_file(void);
int create_big_file(void);

int append_empty_to_small_file(void);
int append_empty_to_big_file(void);
int append_small_to_small_file(void);
int append_small_to_big_file(void);
int append_big_to_big_file(void);

int truncate_small_to_empty_file(void);
int truncate_small_to_small_file(void);
int truncate_big_to_empty_file(void);
int truncate_big_to_small_file(void);
int truncate_big_to_big_file(void);

int slice_expand_1_2(void);
int slice_expand_next_block(void);
int slice_truncate_2_1(void);

int remove_empty_file(void);
int remove_small_file(void);
int remove_big_file(void);
