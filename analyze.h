#ifndef FILES_MOD_ANALYZE_H
#define FILES_MOD_ANALYZE_H

#include "util.h"

#define FM_PE_MAX_SECTIONS 96
#define FM_ASCII_STRING_LIMIT 20
#define FM_ASCII_STRING_CAP 64

typedef struct {
    char name[9];
    uint32_t raw_offset;
    uint32_t raw_size;
    uint32_t characteristics;
} fm_pe_section_t;

typedef struct {
    uint64_t size;
    uint64_t null_bytes;
    uint64_t printable_bytes;
    uint64_t ascii_sequences;
    size_t ascii_string_count;
    char ascii_strings[FM_ASCII_STRING_LIMIT][FM_ASCII_STRING_CAP];
    char type[64];
    int pe_valid;
    uint32_t pe_offset;
    uint16_t pe_sections;
    uint16_t pe_magic;
    fm_pe_section_t sections[FM_PE_MAX_SECTIONS];
} fm_analysis_t;

int fm_analyze_file(const char *path, fm_analysis_t *analysis,
                    char *error, size_t error_capacity);

#endif
