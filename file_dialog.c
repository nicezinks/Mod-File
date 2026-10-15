#include "file_dialog.h"
#include "util.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <string.h>

int fm_select_file(const wchar_t *title, int dump_only, int english, char *output, size_t output_capacity) {
    OPENFILENAMEW dialog;
    wchar_t file_name[FM_PATH_CAP];
    static const wchar_t all_filter_pt[] = L"Todos os arquivos (*.*)\0*.*\0\0";
    static const wchar_t all_filter_en[] = L"All files (*.*)\0*.*\0\0";
    static const wchar_t dump_filter_pt[] = L"Dumps files-mod (*.txt)\0*.txt\0Todos os arquivos (*.*)\0*.*\0\0";
    static const wchar_t dump_filter_en[] = L"files-mod dumps (*.txt)\0*.txt\0All files (*.*)\0*.*\0\0";
    if (output == NULL || output_capacity == 0) return -1;
    output[0] = '\0';
    memset(file_name, 0, sizeof(file_name));
    memset(&dialog, 0, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFile = file_name;
    dialog.nMaxFile = (DWORD)(sizeof(file_name) / sizeof(file_name[0]));
    dialog.lpstrFilter = dump_only ? (english ? dump_filter_en : dump_filter_pt) :
                                              (english ? all_filter_en : all_filter_pt);
    dialog.nFilterIndex = 1;
    dialog.lpstrTitle = title;
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&dialog)) {
        DWORD reason = CommDlgExtendedError();
        return reason == 0 ? 0 : -1;
    }
    if (!fm_wide_to_utf8(file_name, output, output_capacity)) return -1;
    return 1;
}
#else
int fm_select_file(const wchar_t *title, int dump_only, int english, char *output, size_t output_capacity) {
    (void)title;
    (void)dump_only;
    (void)english;
    (void)output;
    (void)output_capacity;
    return -1;
}
#endif
