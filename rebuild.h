#ifndef FILES_MOD_REBUILD_H
#define FILES_MOD_REBUILD_H

#include "util.h"

int fm_rebuild_dump(const char *dump_path, const char *output_dir,
                    char *output_path, size_t output_path_capacity,
                    uint64_t *written_size, char *error, size_t error_capacity);

#endif
