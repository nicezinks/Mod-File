#define _POSIX_C_SOURCE 200809L
#include "analyze.h"

#include <errno.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#else
#include <sys/types.h>
#endif

static uint16_t fm_read_u16le(const unsigned char *data) {
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

static uint32_t fm_read_u32le(const unsigned char *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int fm_seek_file(FILE *file, uint64_t offset) {
#ifdef _WIN32
    if (offset > INT64_MAX) return 0;
    return _fseeki64(file, (long long)offset, SEEK_SET) == 0;
#else
    if (offset > (uint64_t)INT64_MAX) return 0;
    return fseeko(file, (off_t)offset, SEEK_SET) == 0;
#endif
}

static void fm_finish_ascii_sequence(fm_analysis_t *analysis, const char *sequence,
                                     size_t stored_length, uint64_t total_length) {
    if (total_length < 4) return;
    if (analysis->ascii_sequences != UINT64_MAX) ++analysis->ascii_sequences;
    if (analysis->ascii_string_count < FM_ASCII_STRING_LIMIT) {
        char *output = analysis->ascii_strings[analysis->ascii_string_count++];
        size_t length = stored_length;
        if (total_length > stored_length && length > FM_ASCII_STRING_CAP - 4)
            length = FM_ASCII_STRING_CAP - 4;
        memcpy(output, sequence, length);
        if (total_length > stored_length) {
            output[length++] = '.';
            output[length++] = '.';
            output[length++] = '.';
        }
        output[length] = '\0';
    }
}

static int fm_read_at(FILE *file, uint64_t offset, unsigned char *buffer, size_t size) {
    if (!fm_seek_file(file, offset)) return 0;
    return fread(buffer, 1, size, file) == size;
}

static void fm_detect_pe(FILE *file, uint64_t size, fm_analysis_t *analysis) {
    unsigned char dos[64];
    unsigned char coff[24];
    unsigned char optional_prefix[2];
    unsigned char section[40];
    uint32_t pe_offset;
    uint16_t section_count;
    uint16_t optional_size;
    uint64_t section_table;
    size_t index;
    if (size < sizeof(dos) || !fm_read_at(file, 0, dos, sizeof(dos)) ||
        dos[0] != 'M' || dos[1] != 'Z') return;
    pe_offset = fm_read_u32le(dos + 0x3c);
    if ((uint64_t)pe_offset > size || size - (uint64_t)pe_offset < sizeof(coff)) return;
    if (!fm_read_at(file, pe_offset, coff, sizeof(coff)) ||
        coff[0] != 'P' || coff[1] != 'E' || coff[2] != 0 || coff[3] != 0) return;
    section_count = fm_read_u16le(coff + 6);
    optional_size = fm_read_u16le(coff + 20);
    if (section_count > FM_PE_MAX_SECTIONS) return;
    if (optional_size < sizeof(optional_prefix) ||
        (uint64_t)pe_offset + sizeof(coff) + optional_size > size) return;
    if (!fm_read_at(file, (uint64_t)pe_offset + sizeof(coff), optional_prefix, sizeof(optional_prefix))) return;
    analysis->pe_magic = fm_read_u16le(optional_prefix);
    if (analysis->pe_magic != 0x10b && analysis->pe_magic != 0x20b) return;
    section_table = (uint64_t)pe_offset + sizeof(coff) + optional_size;
    if (section_table > size || (uint64_t)section_count * sizeof(section) > size - section_table) return;
    analysis->pe_valid = 1;
    analysis->pe_offset = pe_offset;
    analysis->pe_sections = section_count;
    for (index = 0; index < section_count; ++index) {
        fm_pe_section_t *out = &analysis->sections[index];
        size_t name_index;
        if (!fm_read_at(file, section_table + index * sizeof(section), section, sizeof(section))) {
            analysis->pe_valid = 0;
            analysis->pe_sections = 0;
            return;
        }
        for (name_index = 0; name_index < 8 && section[name_index] != 0; ++name_index)
            out->name[name_index] = (char)section[name_index];
        out->name[name_index] = '\0';
        out->raw_size = fm_read_u32le(section + 16);
        out->raw_offset = fm_read_u32le(section + 20);
        out->characteristics = fm_read_u32le(section + 36);
        if (out->raw_size != 0 &&
            ((uint64_t)out->raw_offset > size ||
             (uint64_t)out->raw_size > size - (uint64_t)out->raw_offset)) {
            analysis->pe_valid = 0;
            analysis->pe_sections = 0;
            return;
        }
    }
}

int fm_analyze_file(const char *path, fm_analysis_t *analysis,
                    char *error, size_t error_capacity) {
    FILE *file = NULL;
    unsigned char first[16];
    size_t first_size;
    unsigned char block[65536];
    uint64_t size;
    size_t got;
    uint64_t run_length = 0;
    char current_sequence[FM_ASCII_STRING_CAP];
    size_t sequence_stored = 0;
    int success = 0;
    if (path == NULL || analysis == NULL) {
        fm_set_error(error, error_capacity, "invalid analysis arguments");
        return 0;
    }
    memset(analysis, 0, sizeof(*analysis));
    file = fm_open_file(path, "rb");
    if (file == NULL) {
        fm_set_error(error, error_capacity, "could not open file: %s", strerror(errno));
        return 0;
    }
    if (!fm_file_size(file, &size)) {
        fm_set_error(error, error_capacity, "could not retrieve file size");
        goto cleanup;
    }
    analysis->size = size;
    first_size = fread(first, 1, sizeof(first), file);
    if (ferror(file)) {
        fm_set_error(error, error_capacity, "could not read file signature");
        goto cleanup;
    }
    if (size == 0) (void)snprintf(analysis->type, sizeof(analysis->type), "empty file");
    else if (first_size >= 8 && memcmp(first, "\x89PNG\r\n\x1a\n", 8) == 0)
        (void)snprintf(analysis->type, sizeof(analysis->type), "png image");
    else if (first_size >= 3 && first[0] == 0xff && first[1] == 0xd8 && first[2] == 0xff)
        (void)snprintf(analysis->type, sizeof(analysis->type), "jpeg image");
    else if (first_size >= 4 && first[0] == 'P' && first[1] == 'K' &&
             ((first[2] == 3 && first[3] == 4) || (first[2] == 5 && first[3] == 6) ||
              (first[2] == 7 && first[3] == 8)))
        (void)snprintf(analysis->type, sizeof(analysis->type), "zip archive");
    else if (first_size >= 5 && memcmp(first, "%PDF-", 5) == 0)
        (void)snprintf(analysis->type, sizeof(analysis->type), "pdf document");
    else if (first_size >= 12 && memcmp(first, "RIFF", 4) == 0 && memcmp(first + 8, "WAVE", 4) == 0)
        (void)snprintf(analysis->type, sizeof(analysis->type), "wave audio");
    else if (first_size >= 3 && memcmp(first, "ID3", 3) == 0)
        (void)snprintf(analysis->type, sizeof(analysis->type), "mpeg audio with id3 tag");
    else if (first_size >= 2 && first[0] == 0xff && (first[1] & 0xe0U) == 0xe0U)
        (void)snprintf(analysis->type, sizeof(analysis->type), "probable mpeg audio frame");
    else if (first_size >= 2 && first[0] == 'M' && first[1] == 'Z')
        (void)snprintf(analysis->type, sizeof(analysis->type), "possible windows executable");
    else
        (void)snprintf(analysis->type, sizeof(analysis->type), "unknown binary data");

    if (!fm_seek_file(file, 0)) {
        fm_set_error(error, error_capacity, "could not seek to the start of the file");
        goto cleanup;
    }
    while ((got = fread(block, 1, sizeof(block), file)) > 0) {
        size_t index;
        for (index = 0; index < got; ++index) {
            unsigned char value = block[index];
            int printable = (value >= 32 && value <= 126) || value == '\t' || value == '\r' || value == '\n';
            if (value == 0) ++analysis->null_bytes;
            if (printable) ++analysis->printable_bytes;
            if (value >= 32 && value <= 126) {
                if (run_length != UINT64_MAX) ++run_length;
                if (sequence_stored < FM_ASCII_STRING_CAP - 4)
                    current_sequence[sequence_stored++] = (char)value;
            } else {
                fm_finish_ascii_sequence(analysis, current_sequence, sequence_stored, run_length);
                run_length = 0;
                sequence_stored = 0;
            }
        }
    }
    if (ferror(file)) {
        fm_set_error(error, error_capacity, "could not scan file bytes");
        goto cleanup;
    }
    fm_finish_ascii_sequence(analysis, current_sequence, sequence_stored, run_length);
    fm_detect_pe(file, size, analysis);
    if (analysis->pe_valid) (void)snprintf(analysis->type, sizeof(analysis->type), "windows pe executable");
    else if (size > 0 && analysis->null_bytes == 0 && ((double)analysis->printable_bytes / (double)size) >= 0.85 &&
             strcmp(analysis->type, "png image") != 0 && strcmp(analysis->type, "jpeg image") != 0 &&
             strcmp(analysis->type, "zip archive") != 0 && strcmp(analysis->type, "pdf document") != 0 &&
             strcmp(analysis->type, "wave audio") != 0 && strcmp(analysis->type, "mpeg audio with id3 tag") != 0)
        (void)snprintf(analysis->type, sizeof(analysis->type), "probable text data");
    success = 1;
cleanup:
    if (file != NULL) (void)fclose(file);
    return success;
}
