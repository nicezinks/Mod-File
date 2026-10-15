#ifndef FILES_MOD_FILE_DIALOG_H
#define FILES_MOD_FILE_DIALOG_H

#include <stddef.h>

int fm_select_file(const wchar_t *title, int dump_only, int english, char *output, size_t output_capacity);

#endif
