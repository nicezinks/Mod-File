#ifndef FILES_MOD_DUMP_H
#define FILES_MOD_DUMP_H

#include "util.h"

int fm_parse_dump(FILE *input, FILE *output, fm_dump_info_t *info,
                  char *error, size_t error_capacity);
int fm_export_dump(const char *source_path, const char *output_dir,
                   char *dump_path, size_t dump_path_capacity,
                   uint64_t *exported_size, char *error, size_t error_capacity);

#endif
