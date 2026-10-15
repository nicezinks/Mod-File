#define _POSIX_C_SOURCE 200809L
#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#endif

void fm_set_error(char *output, size_t capacity, const char *format, ...) {
    va_list args;
    if (output == NULL || capacity == 0) return;
    va_start(args, format);
    (void)vsnprintf(output, capacity, format, args);
    va_end(args);
    output[capacity - 1] = '\0';
}

void fm_ascii_lower(char *text) {
    unsigned char *cursor = (unsigned char *)text;
    if (cursor == NULL) return;
    while (*cursor != 0) {
        if (*cursor >= 'A' && *cursor <= 'Z') *cursor = (unsigned char)(*cursor - 'A' + 'a');
        ++cursor;
    }
}

uint64_t fm_hash_update(uint64_t hash, const unsigned char *data, size_t size) {
    size_t index;
    for (index = 0; index < size; ++index) {
        hash ^= data[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

int fm_utf8_to_wide(const char *input, wchar_t *output, size_t output_count) {
#ifdef _WIN32
    int needed;
    if (input == NULL || output == NULL || output_count == 0 || output_count > INT_MAX) return 0;
    needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input, -1, output, (int)output_count);
    return needed > 0;
#else
    (void)input;
    (void)output;
    (void)output_count;
    return 0;
#endif
}

int fm_wide_to_utf8(const wchar_t *input, char *output, size_t output_capacity) {
#ifdef _WIN32
    int needed;
    if (input == NULL || output == NULL || output_capacity == 0 || output_capacity > INT_MAX) return 0;
    needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, output,
                                 (int)output_capacity, NULL, NULL);
    return needed > 0;
#else
    (void)input;
    (void)output;
    (void)output_capacity;
    return 0;
#endif
}

FILE *fm_open_file(const char *path, const char *mode) {
    if (path == NULL || mode == NULL) return NULL;
#ifdef _WIN32
    {
        wchar_t wide_path[FM_PATH_CAP];
        wchar_t wide_mode[16];
        if (!fm_utf8_to_wide(path, wide_path, FM_PATH_CAP)) return NULL;
        if (!fm_utf8_to_wide(mode, wide_mode, sizeof(wide_mode) / sizeof(wide_mode[0]))) return NULL;
        return _wfopen(wide_path, wide_mode);
    }
#else
    return fopen(path, mode);
#endif
}

int fm_open_exclusive(const char *path, FILE **output) {
    if (path == NULL || output == NULL) return 0;
    *output = NULL;
#ifdef _WIN32
    {
        wchar_t wide_path[FM_PATH_CAP];
        int descriptor;
        if (!fm_utf8_to_wide(path, wide_path, FM_PATH_CAP)) {
            errno = EINVAL;
            return 0;
        }
        descriptor = _wopen(wide_path, _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY,
                             _S_IREAD | _S_IWRITE);
        if (descriptor < 0) return 0;
        *output = _fdopen(descriptor, "wb");
        if (*output == NULL) {
            int saved_errno = errno;
            _close(descriptor);
            (void)_wunlink(wide_path);
            errno = saved_errno;
            return 0;
        }
        return 1;
    }
#else
    {
        int descriptor = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
        if (descriptor < 0) return 0;
        *output = fdopen(descriptor, "wb");
        if (*output == NULL) {
            int saved_errno = errno;
            close(descriptor);
            (void)unlink(path);
            errno = saved_errno;
            return 0;
        }
        return 1;
    }
#endif
}

int fm_path_exists(const char *path) {
    if (path == NULL) return 0;
#ifdef _WIN32
    {
        wchar_t wide_path[FM_PATH_CAP];
        DWORD attributes;
        if (!fm_utf8_to_wide(path, wide_path, FM_PATH_CAP)) return 0;
        attributes = GetFileAttributesW(wide_path);
        return attributes != INVALID_FILE_ATTRIBUTES;
    }
#else
    {
        struct stat status;
        return lstat(path, &status) == 0;
    }
#endif
}

int fm_mkdir(const char *path) {
    if (path == NULL || path[0] == '\0') return 0;
#ifdef _WIN32
    {
        wchar_t wide_path[FM_PATH_CAP];
        DWORD attributes;
        if (!fm_utf8_to_wide(path, wide_path, FM_PATH_CAP)) return 0;
        if (CreateDirectoryW(wide_path, NULL)) return 1;
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            attributes = GetFileAttributesW(wide_path);
            return attributes != INVALID_FILE_ATTRIBUTES &&
                   (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }
        return 0;
    }
#else
    if (mkdir(path, 0700) == 0) return 1;
    if (errno == EEXIST) {
        struct stat status;
        return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
    }
    return 0;
#endif
}

int fm_remove_file(const char *path) {
    if (path == NULL) return 0;
#ifdef _WIN32
    {
        wchar_t wide_path[FM_PATH_CAP];
        if (!fm_utf8_to_wide(path, wide_path, FM_PATH_CAP)) return 0;
        return DeleteFileW(wide_path) != 0;
    }
#else
    return unlink(path) == 0;
#endif
}

int fm_move_no_replace(const char *source, const char *destination) {
    if (source == NULL || destination == NULL) return 0;
#ifdef _WIN32
    {
        wchar_t wide_source[FM_PATH_CAP];
        wchar_t wide_destination[FM_PATH_CAP];
        DWORD error;
        if (!fm_utf8_to_wide(source, wide_source, FM_PATH_CAP) ||
            !fm_utf8_to_wide(destination, wide_destination, FM_PATH_CAP)) {
            errno = EINVAL;
            return 0;
        }
        if (MoveFileW(wide_source, wide_destination)) return 1;
        error = GetLastError();
        if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) errno = EEXIST;
        else if (error == ERROR_ACCESS_DENIED) errno = EACCES;
        else if (error == ERROR_PATH_NOT_FOUND || error == ERROR_FILE_NOT_FOUND) errno = ENOENT;
        else if (error == ERROR_DISK_FULL) errno = ENOSPC;
        else errno = EIO;
        return 0;
    }
#else
    if (link(source, destination) != 0) return 0;
    if (unlink(source) != 0) {
        (void)unlink(destination);
        return 0;
    }
    return 1;
#endif
}

int fm_full_path(const char *path, char *output, size_t capacity) {
    if (path == NULL || output == NULL || capacity == 0) return 0;
#ifdef _WIN32
    {
        wchar_t wide_input[FM_PATH_CAP];
        wchar_t wide_output[FM_PATH_CAP];
        DWORD result;
        if (!fm_utf8_to_wide(path, wide_input, FM_PATH_CAP)) return 0;
        result = GetFullPathNameW(wide_input, (DWORD)FM_PATH_CAP, wide_output, NULL);
        if (result == 0 || result >= FM_PATH_CAP) return 0;
        return fm_wide_to_utf8(wide_output, output, capacity);
    }
#else
    if (path[0] == '/') {
        if (strlen(path) >= capacity) return 0;
        memcpy(output, path, strlen(path) + 1);
        return 1;
    }
    {
        char cwd[FM_PATH_CAP];
        int written;
        if (getcwd(cwd, sizeof(cwd)) == NULL) return 0;
        written = snprintf(output, capacity, "%s/%s", cwd, path);
        return written >= 0 && (size_t)written < capacity;
    }
#endif
}

int fm_join_path(char *output, size_t capacity, const char *left, const char *right) {
    size_t left_length;
    int written;
    char separator;
    if (output == NULL || left == NULL || right == NULL || capacity == 0) return 0;
    left_length = strlen(left);
    separator = '/';
#ifdef _WIN32
    separator = '\\';
#endif
    if (left_length > 0 && (left[left_length - 1] == '/' || left[left_length - 1] == '\\'))
        written = snprintf(output, capacity, "%s%s", left, right);
    else
        written = snprintf(output, capacity, "%s%c%s", left, separator, right);
    return written >= 0 && (size_t)written < capacity;
}

int fm_get_output_dir(char *output, size_t capacity, char *error, size_t error_capacity) {
#ifdef _WIN32
    wchar_t executable[FM_PATH_CAP];
    wchar_t fallback[FM_PATH_CAP];
    DWORD length;
    char utf8[FM_PATH_CAP];
    char candidate[FM_PATH_CAP];
    char *last_slash;
    length = GetModuleFileNameW(NULL, executable, FM_PATH_CAP);
    if (length > 0 && length < FM_PATH_CAP && fm_wide_to_utf8(executable, utf8, sizeof(utf8))) {
        last_slash = strrchr(utf8, '\\');
        if (last_slash == NULL) last_slash = strrchr(utf8, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            if (fm_join_path(candidate, sizeof(candidate), utf8, "files-mod-output") &&
                fm_mkdir(candidate) && fm_full_path(candidate, output, capacity)) return 1;
        }
    }
    length = GetEnvironmentVariableW(L"LOCALAPPDATA", fallback, FM_PATH_CAP);
    if (length > 0 && length < FM_PATH_CAP && fm_wide_to_utf8(fallback, utf8, sizeof(utf8)) &&
        fm_join_path(candidate, sizeof(candidate), utf8, "files-mod-output") &&
        fm_mkdir(candidate) && fm_full_path(candidate, output, capacity)) return 1;
    length = GetTempPathW(FM_PATH_CAP, fallback);
    if (length > 0 && length < FM_PATH_CAP && fm_wide_to_utf8(fallback, utf8, sizeof(utf8)) &&
        fm_join_path(candidate, sizeof(candidate), utf8, "files-mod-output") &&
        fm_mkdir(candidate) && fm_full_path(candidate, output, capacity)) return 1;
    fm_set_error(error, error_capacity, "could not create a writable files-mod-output directory");
    return 0;
#else
    char cwd[FM_PATH_CAP];
    char candidate[FM_PATH_CAP];
    if (getcwd(cwd, sizeof(cwd)) != NULL &&
        fm_join_path(candidate, sizeof(candidate), cwd, "files-mod-output") &&
        fm_mkdir(candidate) && fm_full_path(candidate, output, capacity)) return 1;
    fm_set_error(error, error_capacity, "could not create a writable files-mod-output directory");
    return 0;
#endif
}

int fm_create_unique_file(const char *directory, const char *prefix, const char *extension,
                          char *path, size_t path_capacity, FILE **file,
                          char *error, size_t error_capacity) {
    unsigned int attempt;
    unsigned long seed = (unsigned long)time(NULL);
    if (path != NULL && path_capacity > 0) path[0] = '\0';
    if (file != NULL) *file = NULL;
    if (directory == NULL || prefix == NULL || extension == NULL || path == NULL ||
        path_capacity == 0 || file == NULL) {
        fm_set_error(error, error_capacity, "invalid arguments for unique file creation");
        return 0;
    }
    for (attempt = 0; attempt < 10000U; ++attempt) {
        int written;
        unsigned long salt = seed ^ ((unsigned long)clock() << 7) ^ ((unsigned long)rand() << 16) ^ attempt;
        char filename[FM_NAME_CAP];
        written = snprintf(filename, sizeof(filename), "%s_%08lx_%04u%s", prefix, salt, attempt, extension);
        if (written < 0 || (size_t)written >= sizeof(filename) ||
            !fm_join_path(path, path_capacity, directory, filename)) {
            path[0] = '\0';
            fm_set_error(error, error_capacity, "output path is too long");
            return 0;
        }
        if (fm_open_exclusive(path, file)) return 1;
        if (errno != EEXIST) {
            int saved_errno = errno;
            path[0] = '\0';
            fm_set_error(error, error_capacity, "could not create output file: %s", strerror(saved_errno));
            errno = saved_errno;
            return 0;
        }
    }
    path[0] = '\0';
    fm_set_error(error, error_capacity, "could not find a unique output filename");
    return 0;
}

void fm_safe_basename(const char *input, char *output, size_t capacity) {
    const char *base;
    size_t used = 0;
    size_t index;
    if (output == NULL || capacity == 0) return;
    output[0] = '\0';
    if (input == NULL) return;
    base = input;
    for (index = 0; input[index] != '\0'; ++index) {
        if (input[index] == '/' || input[index] == '\\') base = input + index + 1;
    }
    for (index = 0; base[index] != '\0' && used + 1 < capacity; ++index) {
        unsigned char value = (unsigned char)base[index];
        if (value < 32 || value == '<' || value == '>' || value == ':' || value == '"' ||
            value == '/' || value == '\\' || value == '|' || value == '?' || value == '*') {
            output[used++] = '_';
        } else {
            output[used++] = (char)value;
        }
    }
    while (used > 0 && (output[used - 1] == ' ' || output[used - 1] == '.')) --used;
    output[used] = '\0';
    if (output[0] == '\0') (void)snprintf(output, capacity, "rebuilt_file.bin");
    {
        char upper[16];
        size_t length = strlen(output);
        size_t copy_length = length < sizeof(upper) - 1 ? length : sizeof(upper) - 1;
        memcpy(upper, output, copy_length);
        upper[copy_length] = '\0';
        fm_ascii_lower(upper);
        if (strcmp(upper, "con") == 0 || strcmp(upper, "prn") == 0 ||
            strcmp(upper, "aux") == 0 || strcmp(upper, "nul") == 0 ||
            (strlen(upper) == 4 && (strncmp(upper, "com", 3) == 0 || strncmp(upper, "lpt", 3) == 0) &&
             upper[3] >= '1' && upper[3] <= '9')) {
            if (used + 1 < capacity) {
                memmove(output + 1, output, used + 1);
                output[0] = '_';
            }
        }
    }
}

int fm_make_modified_name(const char *source_name, unsigned int suffix_index,
                          char *output, size_t capacity) {
    char safe[FM_NAME_CAP];
    char stem[FM_NAME_CAP];
    char extension[64] = ".bin";
    char *dot;
    int written;
    if (output == NULL || capacity == 0) return 0;
    output[0] = '\0';
    fm_safe_basename(source_name, safe, sizeof(safe));
    if (safe[0] == '\0') (void)snprintf(safe, sizeof(safe), "rebuilt_file");
    dot = strrchr(safe, '.');
    if (dot != NULL && dot != safe && strlen(dot) <= 32) {
        size_t ext_length = strlen(dot);
        memcpy(extension, dot, ext_length + 1);
        *dot = '\0';
    }
    (void)snprintf(stem, sizeof(stem), "%s", safe);
    if (suffix_index == 0)
        written = snprintf(output, capacity, "%s_modified%s", stem, extension);
    else
        written = snprintf(output, capacity, "%s_modified_%u%s", stem, suffix_index, extension);
    return written >= 0 && (size_t)written < capacity;
}

int fm_file_size(FILE *file, uint64_t *size) {
    if (file == NULL || size == NULL) return 0;
#ifdef _WIN32
    {
        intptr_t os_handle = _get_osfhandle(_fileno(file));
        LARGE_INTEGER value;
        if (os_handle == -1 || !GetFileSizeEx((HANDLE)os_handle, &value) || value.QuadPart < 0) return 0;
        *size = (uint64_t)value.QuadPart;
        return 1;
    }
#else
    {
        off_t current = ftello(file);
        off_t end;
        if (current < 0 || fseeko(file, 0, SEEK_END) != 0) return 0;
        end = ftello(file);
        if (end < 0 || fseeko(file, current, SEEK_SET) != 0) return 0;
        *size = (uint64_t)end;
        return 1;
    }
#endif
}

int fm_write_all(FILE *file, const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *)data;
    size_t written = 0;
    if (file == NULL || (data == NULL && size != 0)) return 0;
    while (written < size) {
        size_t amount = fwrite(bytes + written, 1, size - written, file);
        if (amount == 0) return 0;
        written += amount;
    }
    return 1;
}

int fm_open_output_directory(const char *path) {
#ifdef _WIN32
    wchar_t wide_path[FM_PATH_CAP];
    HINSTANCE result;
    if (!fm_utf8_to_wide(path, wide_path, FM_PATH_CAP)) return 0;
    result = ShellExecuteW(NULL, L"open", wide_path, NULL, NULL, SW_SHOWNORMAL);
    return (INT_PTR)result > 32;
#else
    (void)path;
    return 0;
#endif
}
