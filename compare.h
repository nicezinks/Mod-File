#ifndef FILES_MOD_COMPARE_H
#define FILES_MOD_COMPARE_H

#include "util.h"

#define FM_COMPARE_DISPLAY_LIMIT 100
#define FM_COMPARE_REPORT_LIMIT 10000

typedef struct {
    uint64_t offset;
    int left_value;
    int right_value;
} fm_difference_t;

typedef struct {
    uint64_t left_size;
    uint64_t right_size;
    uint64_t different_positions;
    int identical;
    size_t shown_count;
    fm_difference_t shown[FM_COMPARE_DISPLAY_LIMIT];
    char report_path[FM_PATH_CAP];
    int report_truncated;
} fm_compare_result_t;

int fm_compare_files(const char *left_path, const char *right_path, const char *output_dir,
                     fm_compare_result_t *result, char *error, size_t error_capacity);

#endif
