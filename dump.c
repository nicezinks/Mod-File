#include "dump.h"
#include "analyze.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>


static int fm_read_line(FILE *file, char *output, size_t capacity, size_t *line_length) {
    size_t used = 0;
    int value;
    if (file == NULL || output == NULL || capacity < 2 || line_length == NULL) return -1;
    while ((value = fgetc(file)) != EOF) {
        if (value == 0) return -2;
        if (used + 1 >= capacity) return -3;
        output[used++] = (char)value;
        if (value == '\n') break;
    }
    if (ferror(file)) return -1;
    if (used == 0) return 0;
    output[used] = '\0';
    *line_length = used;
    return 1;
}

static int fm_hex_value(unsigned char value) {
    if (value >= '0' && value <= '9') return (int)(value - '0');
    if (value >= 'a' && value <= 'f') return (int)(value - 'a' + 10);
    if (value >= 'A' && value <= 'F') return (int)(value - 'A' + 10);
    return -1;
}

static int fm_parse_u64_decimal(const char *text, uint64_t *value) {
    uint64_t result = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    if (text == NULL || *text == '\0') return 0;
    while (*cursor != '\0') {
        unsigned int digit;
        if (*cursor < '0' || *cursor > '9') return 0;
        digit = (unsigned int)(*cursor - '0');
        if (result > (UINT64_MAX - digit) / 10U) return 0;
        result = result * 10U + digit;
        ++cursor;
    }
    *value = result;
    return 1;
}

static int fm_parse_offset(const char *text, uint64_t *offset, const char **after_colon) {
    size_t digits = 0;
    uint64_t value = 0;
    const unsigned char *cursor = (const unsigned char *)text;
    while (cursor[digits] != '\0' && cursor[digits] != ':') ++digits;
    if (cursor[digits] != ':' || digits < 8 || digits > 16) return 0;
    while (*cursor != ':') {
        int hex = fm_hex_value(*cursor);
        if (hex < 0) return 0;
        value = (value << 4) | (uint64_t)hex;
        ++cursor;
    }
    *offset = value;
    *after_colon = (const char *)cursor + 1;
    return 1;
}

static int fm_parse_data_line(char *line, uint64_t expected_offset, FILE *output,
                              uint64_t *count, size_t line_number,
                              char *error, size_t error_capacity) {
    const char *cursor;
    uint64_t offset;
    unsigned int row_count = 0;
    unsigned char row_bytes[16];
    if (!fm_parse_offset(line, &offset, &cursor)) {
        fm_set_error(error, error_capacity, "invalid data row at line %zu", line_number);
        return 0;
    }
    if (offset != expected_offset) {
        fm_set_error(error, error_capacity, "unexpected offset at line %zu expected %08llx",
                     line_number, (unsigned long long)expected_offset);
        return 0;
    }
    while (*cursor != '\0' && *cursor != '\n' && *cursor != '\r') {
        int high;
        int low;
        size_t token_length = 0;
        while (*cursor != '\0' && isspace((unsigned char)*cursor)) ++cursor;
        if (*cursor == '\0' || *cursor == '\n' || *cursor == '\r' || *cursor == '|') break;
        while (cursor[token_length] != '\0' && !isspace((unsigned char)cursor[token_length]) &&
               cursor[token_length] != '|') ++token_length;
        if (token_length != 2) {
            fm_set_error(error, error_capacity, "invalid hexadecimal byte at line %zu", line_number);
            return 0;
        }
        high = fm_hex_value((unsigned char)cursor[0]);
        low = fm_hex_value((unsigned char)cursor[1]);
        if (high < 0 || low < 0) {
            fm_set_error(error, error_capacity, "invalid hexadecimal byte at line %zu", line_number);
            return 0;
        }
        if (row_count >= 16) {
            fm_set_error(error, error_capacity, "more than sixteen bytes at line %zu", line_number);
            return 0;
        }
        row_bytes[row_count++] = (unsigned char)((high << 4) | low);
        cursor += token_length;
        if (*cursor == '|') break;
    }
    if (row_count == 0) {
        fm_set_error(error, error_capacity, "data row has no bytes at line %zu", line_number);
        return 0;
    }
    if (*count > UINT64_MAX - row_count) {
        fm_set_error(error, error_capacity, "reconstructed size exceeds supported limits at line %zu", line_number);
        return 0;
    }
    if (output != NULL && !fm_write_all(output, row_bytes, row_count)) {
        fm_set_error(error, error_capacity, "could not write reconstructed bytes");
        return 0;
    }
    *count += row_count;
    return (int)row_count;
}

int fm_parse_dump(FILE *input, FILE *output, fm_dump_info_t *info,
                  char *error, size_t error_capacity) {
    char line[FM_LINE_CAP];
    size_t line_number = 0;
    int data_seen = 0;
    int source_name_seen = 0;
    int source_size_seen = 0;
    unsigned int previous_row_count = 16;
    uint64_t byte_count = 0;
    uint64_t hash = UINT64_C(14695981039346656037);
    fm_dump_info_t result;
    if (input == NULL || info == NULL) {
        fm_set_error(error, error_capacity, "invalid parser arguments");
        return 0;
    }
    memset(&result, 0, sizeof(result));
    for (;;) {
        size_t length = 0;
        int read_result = fm_read_line(input, line, sizeof(line), &length);
        char *text = line;
        if (read_result == 0) break;
        ++line_number;
        if (read_result == -2) {
            fm_set_error(error, error_capacity, "dump contains a null byte at line %zu", line_number);
            return 0;
        }
        if (read_result == -3) {
            fm_set_error(error, error_capacity, "line %zu is too long", line_number);
            return 0;
        }
        if (read_result < 0) {
            fm_set_error(error, error_capacity, "could not read the dump file");
            return 0;
        }
        hash = fm_hash_update(hash, (const unsigned char *)line, length);
        if (line_number == 1 && length >= 3 && (unsigned char)line[0] == 0xef &&
            (unsigned char)line[1] == 0xbb && (unsigned char)line[2] == 0xbf) text += 3;
        while (*text != '\0' && (*text == '\n' || *text == '\r')) ++text;
        {
            size_t text_length = strlen(text);
            while (text_length > 0 && (text[text_length - 1] == '\n' || text[text_length - 1] == '\r'))
                text[--text_length] = '\0';
        }
        if (line_number == 1) {
            if (strcmp(text, "# files-mod dump v1") != 0) {
                fm_set_error(error, error_capacity, "invalid dump header at line 1");
                return 0;
            }
            continue;
        }
        if (text[0] == '\0') continue;
        if (text[0] == '#') {
            static const char name_prefix[] = "# source name ";
            static const char size_prefix[] = "# source size ";
            if (strncmp(text, name_prefix, sizeof(name_prefix) - 1) == 0) {
                const char *name = text + sizeof(name_prefix) - 1;
                if (source_name_seen || name[0] == '\0' || strlen(name) >= sizeof(result.source_name)) {
                    fm_set_error(error, error_capacity, "invalid source name metadata at line %zu", line_number);
                    return 0;
                }
                memcpy(result.source_name, name, strlen(name) + 1);
                source_name_seen = 1;
            } else if (strncmp(text, size_prefix, sizeof(size_prefix) - 1) == 0) {
                uint64_t ignored_size;
                if (source_size_seen || !fm_parse_u64_decimal(text + sizeof(size_prefix) - 1, &ignored_size)) {
                    fm_set_error(error, error_capacity, "invalid source size metadata at line %zu", line_number);
                    return 0;
                }
                source_size_seen = 1;
            } else if (strncmp(text, "# files-mod dump v", 18) == 0) {
                fm_set_error(error, error_capacity, "unsupported dump version at line %zu", line_number);
                return 0;
            }
            continue;
        }
        if (data_seen && previous_row_count < 16) {
            fm_set_error(error, error_capacity, "only the final data row may contain fewer than sixteen bytes at line %zu", line_number);
            return 0;
        }
        {
            int row_count = fm_parse_data_line(text, byte_count, output, &byte_count,
                                               line_number, error, error_capacity);
            if (row_count == 0) return 0;
            previous_row_count = (unsigned int)row_count;
        }
        data_seen = 1;
    }
    if (line_number == 0) {
        fm_set_error(error, error_capacity, "dump file is empty");
        return 0;
    }
    result.size = byte_count;
    result.text_hash = hash;
    *info = result;
    return 1;
}

static void fm_ascii_preview(const unsigned char *bytes, size_t length, char *output, size_t capacity) {
    size_t index;
    size_t used = 0;
    for (index = 0; index < length && used + 1 < capacity; ++index) {
        unsigned char value = bytes[index];
        char shown = (value >= 32 && value <= 126) ? (char)value : '.';
        output[used++] = shown;
    }
    output[used] = '\0';
}

int fm_export_dump(const char *source_path, const char *output_dir,
                   char *dump_path, size_t dump_path_capacity,
                   uint64_t *exported_size, char *error, size_t error_capacity) {
    FILE *source = NULL;
    FILE *dump = NULL;
    char source_abs[FM_PATH_CAP];
    char source_name[FM_NAME_CAP];
    uint64_t original_size;
    uint64_t offset = 0;
    unsigned char bytes[65536];
    fm_analysis_t analysis;
    char analysis_error[256];
    int success = 0;
    if (dump_path != NULL && dump_path_capacity > 0) dump_path[0] = '\0';
    if (source_path == NULL || output_dir == NULL || dump_path == NULL) {
        fm_set_error(error, error_capacity, "invalid export arguments");
        return 0;
    }
    if (!fm_full_path(source_path, source_abs, sizeof(source_abs))) {
        fm_set_error(error, error_capacity, "could not resolve source path");
        return 0;
    }
    source = fm_open_file(source_abs, "rb");
    if (source == NULL) {
        fm_set_error(error, error_capacity, "could not open source file: %s", strerror(errno));
        goto cleanup;
    }
    if (!fm_file_size(source, &original_size)) {
        fm_set_error(error, error_capacity, "could not read source file size");
        goto cleanup;
    }
    fm_safe_basename(source_abs, source_name, sizeof(source_name));
    if (!fm_create_unique_file(output_dir, "files_mod_dump", ".txt", dump_path,
                               dump_path_capacity, &dump, error, error_capacity)) goto cleanup;
    memset(&analysis, 0, sizeof(analysis));
    if (!fm_analyze_file(source_abs, &analysis, analysis_error, sizeof(analysis_error)))
        (void)snprintf(analysis.type, sizeof(analysis.type), "unknown binary data");
    if (fprintf(dump, "# files-mod dump v1\n# source name %s\n# source size %llu\n# source type %s\n# edit byte pairs and keep offsets sequential\n# use sixteen bytes per row except for the final row\n",
                source_name, (unsigned long long)original_size,
                analysis.type[0] != '\0' ? analysis.type : "unknown data") < 0) {
        fm_set_error(error, error_capacity, "could not write dump metadata");
        goto cleanup;
    }
    while (offset < original_size) {
        size_t wanted = (original_size - offset) < sizeof(bytes) ? (size_t)(original_size - offset) : sizeof(bytes);
        size_t got = fread(bytes, 1, wanted, source);
        size_t block_position;
        if (got != wanted) {
            fm_set_error(error, error_capacity, "source file changed or could not be read");
            goto cleanup;
        }
        for (block_position = 0; block_position < got; block_position += 16) {
            static const char hex_digits[] = "0123456789abcdef";
            char row[128];
            char preview[17];
            size_t index;
            size_t row_bytes = got - block_position < 16 ? got - block_position : 16;
            int prefix_length = snprintf(row, sizeof(row), "%08llx:", (unsigned long long)(offset + block_position));
            size_t row_length;
            if (prefix_length < 0 || (size_t)prefix_length >= sizeof(row)) {
                fm_set_error(error, error_capacity, "could not format dump offset");
                goto cleanup;
            }
            fm_ascii_preview(bytes + block_position, row_bytes, preview, sizeof(preview));
            row_length = (size_t)prefix_length;
            for (index = 0; index < row_bytes; ++index) {
                unsigned char value = bytes[block_position + index];
                row[row_length++] = ' ';
                row[row_length++] = hex_digits[value >> 4];
                row[row_length++] = hex_digits[value & 0x0fU];
            }
            row[row_length++] = ' ';
            row[row_length++] = '|';
            memcpy(row + row_length, preview, strlen(preview));
            row_length += strlen(preview);
            row[row_length++] = '\n';
            if (!fm_write_all(dump, row, row_length)) {
                fm_set_error(error, error_capacity, "could not write dump row");
                goto cleanup;
            }
        }
        offset += got;
    }
    if (fgetc(source) != EOF || ferror(source) || offset != original_size) {
        fm_set_error(error, error_capacity, "source file size changed during export");
        goto cleanup;
    }
    if (fflush(dump) != 0 || ferror(dump)) {
        fm_set_error(error, error_capacity, "could not flush dump file");
        goto cleanup;
    }
    if (fclose(dump) != 0) {
        dump = NULL;
        fm_set_error(error, error_capacity, "could not close dump file");
        goto cleanup;
    }
    dump = NULL;
    if (exported_size != NULL) *exported_size = original_size;
    success = 1;
cleanup:
    if (source != NULL) (void)fclose(source);
    if (dump != NULL) (void)fclose(dump);
    if (!success && dump_path != NULL && dump_path[0] != '\0') {
        (void)fm_remove_file(dump_path);
        dump_path[0] = '\0';
    }
    if (success) {
        char absolute_dump[FM_PATH_CAP];
        if (fm_full_path(dump_path, absolute_dump, sizeof(absolute_dump))) {
            size_t length = strlen(absolute_dump);
            if (length < dump_path_capacity) memcpy(dump_path, absolute_dump, length + 1);
        }
    }
    return success;
}
