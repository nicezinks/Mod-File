#include "rebuild.h"
#include "dump.h"

#include <errno.h>
#include <string.h>

int fm_rebuild_dump(const char *dump_path, const char *output_dir,
                    char *output_path, size_t output_path_capacity,
                    uint64_t *written_size, char *error, size_t error_capacity) {
    FILE *first_input = NULL;
    FILE *second_input = NULL;
    FILE *temporary = NULL;
    char temporary_path[FM_PATH_CAP] = "";
    char candidate[FM_PATH_CAP];
    char filename[FM_NAME_CAP];
    fm_dump_info_t first_info;
    fm_dump_info_t second_info;
    uint64_t temporary_size = 0;
    unsigned int attempt;
    int success = 0;
    if (output_path != NULL && output_path_capacity > 0) output_path[0] = '\0';
    if (dump_path == NULL || output_dir == NULL || output_path == NULL || output_path_capacity == 0) {
        fm_set_error(error, error_capacity, "invalid rebuild arguments");
        return 0;
    }
    first_input = fm_open_file(dump_path, "rb");
    if (first_input == NULL) {
        fm_set_error(error, error_capacity, "could not open dump file: %s", strerror(errno));
        goto cleanup;
    }
    if (!fm_parse_dump(first_input, NULL, &first_info, error, error_capacity)) goto cleanup;
    if (fclose(first_input) != 0) {
        first_input = NULL;
        fm_set_error(error, error_capacity, "could not close dump after validation");
        goto cleanup;
    }
    first_input = NULL;

    if (!fm_create_unique_file(output_dir, ".files_mod_rebuild", ".tmp", temporary_path,
                               sizeof(temporary_path), &temporary, error, error_capacity)) goto cleanup;
    second_input = fm_open_file(dump_path, "rb");
    if (second_input == NULL) {
        fm_set_error(error, error_capacity, "could not reopen dump file");
        goto cleanup;
    }
    if (!fm_parse_dump(second_input, temporary, &second_info, error, error_capacity)) goto cleanup;
    if (first_info.text_hash != second_info.text_hash || first_info.size != second_info.size ||
        strcmp(first_info.source_name, second_info.source_name) != 0) {
        fm_set_error(error, error_capacity, "dump changed while it was being rebuilt");
        goto cleanup;
    }
    if (fflush(temporary) != 0 || ferror(temporary)) {
        fm_set_error(error, error_capacity, "could not flush temporary output file");
        goto cleanup;
    }
    if (fclose(temporary) != 0) {
        temporary = NULL;
        fm_set_error(error, error_capacity, "could not close temporary output file");
        goto cleanup;
    }
    temporary = NULL;
    temporary_size = second_info.size;

    if (first_info.source_name[0] == '\0')
        (void)snprintf(filename, sizeof(filename), "rebuilt_file_modified.bin");
    else if (!fm_make_modified_name(first_info.source_name, 0, filename, sizeof(filename))) {
        fm_set_error(error, error_capacity, "source filename is too long");
        goto cleanup;
    }
    for (attempt = 0; attempt < 10000U; ++attempt) {
        if (attempt > 0 && !fm_make_modified_name(first_info.source_name[0] != '\0' ? first_info.source_name : "rebuilt_file.bin",
                                                   attempt, filename, sizeof(filename))) {
            fm_set_error(error, error_capacity, "could not create a unique output name");
            goto cleanup;
        }
        if (!fm_join_path(candidate, sizeof(candidate), output_dir, filename)) {
            fm_set_error(error, error_capacity, "output path is too long");
            goto cleanup;
        }
        if (fm_move_no_replace(temporary_path, candidate)) {
            temporary_path[0] = '\0';
            if (!fm_full_path(candidate, output_path, output_path_capacity)) {
                (void)snprintf(output_path, output_path_capacity, "%s", candidate);
            }
            if (written_size != NULL) *written_size = temporary_size;
            success = 1;
            goto cleanup;
        }
        if (!fm_path_exists(candidate)) {
            fm_set_error(error, error_capacity, "could not finalize output file: %s", strerror(errno));
            goto cleanup;
        }
    }
    fm_set_error(error, error_capacity, "could not find an available output filename");
cleanup:
    if (first_input != NULL) (void)fclose(first_input);
    if (second_input != NULL) (void)fclose(second_input);
    if (temporary != NULL) (void)fclose(temporary);
    if (!success && temporary_path[0] != '\0') (void)fm_remove_file(temporary_path);
    return success;
}
