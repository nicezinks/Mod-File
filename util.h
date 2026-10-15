#ifndef FILES_MOD_UTIL_H
#define FILES_MOD_UTIL_H

#include <stddef.h>
#include <wchar.h>
#include <stdint.h>
#include <stdio.h>

#define FM_PATH_CAP 32768
#define FM_NAME_CAP 1024
#define FM_LINE_CAP 8192

typedef struct {
    uint64_t size;
    uint64_t text_hash;
    char source_name[FM_NAME_CAP];
} fm_dump_info_t;

int fm_utf8_to_wide(const char *input, wchar_t *output, size_t output_count);
int fm_wide_to_utf8(const wchar_t *input, char *output, size_t output_capacity);
FILE *fm_open_file(const char *path, const char *mode);
int fm_open_exclusive(const char *path, FILE **output);
int fm_path_exists(const char *path);
int fm_mkdir(const char *path);
int fm_remove_file(const char *path);
int fm_move_no_replace(const char *source, const char *destination);
int fm_full_path(const char *path, char *output, size_t capacity);
int fm_join_path(char *output, size_t capacity, const char *left, const char *right);
int fm_get_output_dir(char *output, size_t capacity, char *error, size_t error_capacity);
int fm_create_unique_file(const char *directory, const char *prefix, const char *extension,
                          char *path, size_t path_capacity, FILE **file,
                          char *error, size_t error_capacity);
int fm_make_modified_name(const char *source_name, unsigned int suffix_index,
                          char *output, size_t capacity);
int fm_file_size(FILE *file, uint64_t *size);
int fm_write_all(FILE *file, const void *data, size_t size);
#if defined(__GNUC__) || defined(__clang__)
#define FM_PRINTF_FORMAT(format_index, first_argument) \
    __attribute__((format(printf, format_index, first_argument)))
#else
#define FM_PRINTF_FORMAT(format_index, first_argument)
#endif
void fm_set_error(char *output, size_t capacity, const char *format, ...)
    FM_PRINTF_FORMAT(3, 4);
#undef FM_PRINTF_FORMAT
void fm_ascii_lower(char *text);
void fm_safe_basename(const char *input, char *output, size_t capacity);
int fm_open_output_directory(const char *path);
uint64_t fm_hash_update(uint64_t hash, const unsigned char *data, size_t size);

#endif
