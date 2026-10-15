#include "compare.h"

#include <errno.h>
#include <inttypes.h>
#include <string.h>

#define FM_COMPARE_BLOCK 65536U

typedef struct {
    fm_compare_result_t *result;
    FILE *report;
    uint64_t report_written;
    int report_failed;
} fm_compare_context_t;

static int fm_compare_emit(fm_compare_context_t *context, uint64_t offset, int left_value, int right_value) {
    fm_compare_result_t *result = context->result;
    if (result->different_positions == UINT64_MAX) return 0;
    if (result->shown_count < FM_COMPARE_DISPLAY_LIMIT) {
        fm_difference_t *difference = &result->shown[result->shown_count++];
        difference->offset = offset;
        difference->left_value = left_value;
        difference->right_value = right_value;
    }
    if (context->report != NULL && context->report_written < FM_COMPARE_REPORT_LIMIT) {
        if (fprintf(context->report, "offset %08" PRIx64 " left ", offset) < 0) context->report_failed = 1;
        if (left_value < 0) {
            if (fprintf(context->report, "--") < 0) context->report_failed = 1;
        } else if (fprintf(context->report, "%02x", (unsigned int)left_value) < 0) {
            context->report_failed = 1;
        }
        if (fprintf(context->report, " right ") < 0) context->report_failed = 1;
        if (right_value < 0) {
            if (fprintf(context->report, "--\n") < 0) context->report_failed = 1;
        } else if (fprintf(context->report, "%02x\n", (unsigned int)right_value) < 0) {
            context->report_failed = 1;
        }
        ++context->report_written;
    }
    ++result->different_positions;
    return !context->report_failed;
}

static int fm_scan_differences(FILE *left, FILE *right, uint64_t left_size, uint64_t right_size,
                               fm_compare_context_t *context) {
    unsigned char left_block[FM_COMPARE_BLOCK];
    unsigned char right_block[FM_COMPARE_BLOCK];
    uint64_t common_size = left_size < right_size ? left_size : right_size;
    uint64_t offset = 0;
    while (offset < common_size) {
        size_t amount = (common_size - offset) < FM_COMPARE_BLOCK ?
                        (size_t)(common_size - offset) : FM_COMPARE_BLOCK;
        size_t left_got = fread(left_block, 1, amount, left);
        size_t right_got = fread(right_block, 1, amount, right);
        size_t index;
        if (left_got != amount || right_got != amount) return 0;
        for (index = 0; index < amount; ++index) {
            if (left_block[index] != right_block[index] &&
                !fm_compare_emit(context, offset + index, left_block[index], right_block[index])) return 0;
        }
        offset += amount;
    }
    if (left_size > common_size) {
        while (offset < left_size) {
            size_t wanted = (left_size - offset) < FM_COMPARE_BLOCK ?
                            (size_t)(left_size - offset) : FM_COMPARE_BLOCK;
            size_t got = fread(left_block, 1, wanted, left);
            size_t index;
            if (got != wanted) return 0;
            for (index = 0; index < got; ++index)
                if (!fm_compare_emit(context, offset + index, left_block[index], -1)) return 0;
            offset += got;
        }
    } else if (right_size > common_size) {
        while (offset < right_size) {
            size_t wanted = (right_size - offset) < FM_COMPARE_BLOCK ?
                            (size_t)(right_size - offset) : FM_COMPARE_BLOCK;
            size_t got = fread(right_block, 1, wanted, right);
            size_t index;
            if (got != wanted) return 0;
            for (index = 0; index < got; ++index)
                if (!fm_compare_emit(context, offset + index, -1, right_block[index])) return 0;
            offset += got;
        }
    }
    return !ferror(left) && !ferror(right);
}

static int fm_write_report(const char *left_path, const char *right_path, const char *output_dir,
                           uint64_t left_size, uint64_t right_size, fm_compare_result_t *result,
                           char *error, size_t error_capacity) {
    FILE *left = NULL;
    FILE *right = NULL;
    FILE *report = NULL;
    char report_path[FM_PATH_CAP] = "";
    fm_compare_context_t context;
    fm_compare_result_t report_scan_result;
    int success = 0;
    if (!fm_create_unique_file(output_dir, "files_mod_compare", ".txt", report_path,
                               sizeof(report_path), &report, error, error_capacity)) return 0;
    if (fprintf(report, "# files-mod comparison report v1\n# left path %s\n# right path %s\n# left size %llu\n# right size %llu\n# different positions %llu\n",
                left_path, right_path, (unsigned long long)left_size,
                (unsigned long long)right_size, (unsigned long long)result->different_positions) < 0) {
        fm_set_error(error, error_capacity, "could not write comparison report metadata");
        goto cleanup;
    }
    left = fm_open_file(left_path, "rb");
    right = fm_open_file(right_path, "rb");
    if (left == NULL || right == NULL) {
        fm_set_error(error, error_capacity, "could not reopen files for comparison report");
        goto cleanup;
    }
    memset(&context, 0, sizeof(context));
    memset(&report_scan_result, 0, sizeof(report_scan_result));
    context.result = &report_scan_result;
    context.report = report;
    if (!fm_scan_differences(left, right, left_size, right_size, &context)) {
        fm_set_error(error, error_capacity, "could not complete comparison report");
        goto cleanup;
    }
    if (report_scan_result.different_positions != result->different_positions ||
        fgetc(left) != EOF || fgetc(right) != EOF || ferror(left) || ferror(right)) {
        fm_set_error(error, error_capacity, "files changed while the comparison report was being written");
        goto cleanup;
    }
    result->report_truncated = result->different_positions > FM_COMPARE_REPORT_LIMIT;
    if (result->report_truncated &&
        fprintf(report, "# report limited to first %u differences\n", FM_COMPARE_REPORT_LIMIT) < 0) {
        fm_set_error(error, error_capacity, "could not write report summary");
        goto cleanup;
    }
    if (fflush(report) != 0 || ferror(report)) {
        fm_set_error(error, error_capacity, "could not flush comparison report");
        goto cleanup;
    }
    if (fclose(report) != 0) {
        report = NULL;
        fm_set_error(error, error_capacity, "could not close comparison report");
        goto cleanup;
    }
    report = NULL;
    if (!fm_full_path(report_path, result->report_path, sizeof(result->report_path)))
        (void)snprintf(result->report_path, sizeof(result->report_path), "%s", report_path);
    success = 1;
cleanup:
    if (left != NULL) (void)fclose(left);
    if (right != NULL) (void)fclose(right);
    if (report != NULL) (void)fclose(report);
    if (!success) (void)fm_remove_file(report_path);
    return success;
}

int fm_compare_files(const char *left_path, const char *right_path, const char *output_dir,
                     fm_compare_result_t *result, char *error, size_t error_capacity) {
    FILE *left = NULL;
    FILE *right = NULL;
    fm_compare_context_t context;
    int success = 0;
    if (left_path == NULL || right_path == NULL || output_dir == NULL || result == NULL) {
        fm_set_error(error, error_capacity, "invalid comparison arguments");
        return 0;
    }
    memset(result, 0, sizeof(*result));
    left = fm_open_file(left_path, "rb");
    right = fm_open_file(right_path, "rb");
    if (left == NULL || right == NULL) {
        fm_set_error(error, error_capacity, "could not open files for comparison: %s", strerror(errno));
        goto cleanup;
    }
    if (!fm_file_size(left, &result->left_size) || !fm_file_size(right, &result->right_size)) {
        fm_set_error(error, error_capacity, "could not retrieve file sizes");
        goto cleanup;
    }
    memset(&context, 0, sizeof(context));
    context.result = result;
    if (!fm_scan_differences(left, right, result->left_size, result->right_size, &context)) {
        fm_set_error(error, error_capacity, "could not read files or count differences");
        goto cleanup;
    }
    if (fgetc(left) != EOF || fgetc(right) != EOF || ferror(left) || ferror(right)) {
        fm_set_error(error, error_capacity, "a file changed during comparison");
        goto cleanup;
    }
    result->identical = result->left_size == result->right_size && result->different_positions == 0;
    if (result->different_positions > FM_COMPARE_DISPLAY_LIMIT &&
        !fm_write_report(left_path, right_path, output_dir, result->left_size, result->right_size,
                         result, error, error_capacity)) goto cleanup;
    success = 1;
cleanup:
    if (left != NULL) (void)fclose(left);
    if (right != NULL) (void)fclose(right);
    return success;
}
