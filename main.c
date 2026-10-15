#include "analyze.h"
#include "compare.h"
#include "dump.h"
#include "file_dialog.h"
#include "rebuild.h"
#include "util.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static int english;

static const char *fm_text(const char *portuguese, const char *english_text) {
    return english ? english_text : portuguese;
}

static const wchar_t *fm_dialog_title(const wchar_t *portuguese, const wchar_t *english_text) {
    return english ? english_text : portuguese;
}

static void fm_print_error(const char *portuguese, const char *english_text, const char *detail) {
    printf("[!] %s\n", fm_text(portuguese, english_text));
    if (detail != NULL && detail[0] != '\0')
        printf("    %s: %s\n", fm_text("detalhe tecnico", "technical detail"), detail);
}

static const char *fm_type_label(const char *type) {
    if (english) return type;
    if (strcmp(type, "empty file") == 0) return "arquivo vazio";
    if (strcmp(type, "png image") == 0) return "imagem png";
    if (strcmp(type, "jpeg image") == 0) return "imagem jpeg";
    if (strcmp(type, "zip archive") == 0) return "arquivo zip";
    if (strcmp(type, "pdf document") == 0) return "documento pdf";
    if (strcmp(type, "wave audio") == 0) return "audio wave";
    if (strcmp(type, "mpeg audio with id3 tag") == 0) return "audio mpeg com id3";
    if (strcmp(type, "windows pe executable") == 0) return "executavel windows pe";
    if (strcmp(type, "possible windows executable") == 0) return "possivel executavel windows";
    if (strcmp(type, "probable text data") == 0) return "provavel texto";
    return "dados binarios desconhecidos";
}

static int fm_read_choice(const char *prompt, long minimum, long maximum, long *choice) {
    char input[64];
    for (;;) {
        char *end;
        long value;
        printf("%s", prompt);
        if (fgets(input, sizeof(input), stdin) == NULL) return 0;
        if (strchr(input, '\n') == NULL && !feof(stdin)) {
            int character;
            while ((character = getchar()) != '\n' && character != EOF) {}
            puts(fm_text("[!] entrada muito longa", "[!] input too long"));
            continue;
        }
        errno = 0;
        value = strtol(input, &end, 10);
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
        if (errno != 0 || end == input || *end != '\0' || value < minimum || value > maximum) {
            puts(fm_text("[!] opcao invalida", "[!] invalid option"));
            continue;
        }
        *choice = value;
        return 1;
    }
}

static int fm_choose_language(void) {
    long choice;
    puts("files-mod");
    puts("1. Portugues");
    puts("2. English");
    if (!fm_read_choice("idioma / language: ", 1, 2, &choice)) return 0;
    english = choice == 2;
    return 1;
}

static void fm_show_menu(void) {
    puts("");
    puts(fm_text("files-mod - inspecao e reconstrucao binaria", "files-mod - binary inspection and reconstruction"));
    puts(fm_text("1. exportar bytes de um arquivo", "1. export bytes from a file"));
    puts(fm_text("2. reconstruir arquivo a partir de um dump", "2. rebuild file from a dump"));
    puts(fm_text("3. analisar arquivo", "3. inspect a file"));
    puts(fm_text("4. comparar arquivos", "4. compare files"));
    puts(fm_text("5. abrir pasta de saida", "5. open output directory"));
    puts(fm_text("0. sair", "0. exit"));
}

static int fm_select_or_report(int dump_only, char *path, size_t path_capacity,
                               const wchar_t *portuguese_title, const wchar_t *english_title) {
    int selection = fm_select_file(fm_dialog_title(portuguese_title, english_title), dump_only, english,
                                   path, path_capacity);
    if (selection == 0) {
        puts(fm_text("[!] selecao cancelada", "[!] selection canceled"));
        return 0;
    }
    if (selection < 0) {
        fm_print_error("nao foi possivel abrir a janela de selecao",
                        "could not open the file selection dialog",
                        fm_text("verifique o suporte a dialogos do Windows", "check Windows common dialog support"));
        return 0;
    }
    {
        char absolute[FM_PATH_CAP];
        if (!fm_full_path(path, absolute, sizeof(absolute))) {
            fm_print_error("nao foi possivel resolver o caminho", "could not resolve the file path",
                           fm_text("caminho invalido ou longo demais", "invalid or too long path"));
            return 0;
        }
        (void)snprintf(path, path_capacity, "%s", absolute);
    }
    return 1;
}

static void fm_operation_export(const char *output_dir) {
    char source_path[FM_PATH_CAP] = "";
    char dump_path[FM_PATH_CAP] = "";
    char error[512] = "";
    uint64_t size = 0;
    if (!fm_select_or_report(0, source_path, sizeof(source_path),
                             L"selecionar arquivo", L"Select a file")) return;
    puts(fm_text("[+] arquivo selecionado", "[+] file selected"));
    printf("%s: %s\n", fm_text("caminho do arquivo", "file path"), source_path);
    puts(fm_text("[+] lendo bytes do arquivo", "[+] reading file bytes"));
    if (!fm_export_dump(source_path, output_dir, dump_path, sizeof(dump_path), &size, error, sizeof(error))) {
        fm_print_error("nao foi possivel exportar o dump", "could not export the dump", error);
        return;
    }
    puts(fm_text("[+] dump criado com sucesso", "[+] dump created successfully"));
    printf("%s: %s\n", fm_text("caminho do dump", "dump path"), dump_path);
    printf("%s: %" PRIu64 " bytes\n", fm_text("tamanho original", "original size"), size);
    puts(fm_text("edite os bytes no txt e depois volte ao programa", "edit the bytes in the txt and return to the program"));
    puts(fm_text("a edicao pode invalidar assinaturas e estruturas internas",
                 "editing may invalidate signatures and internal structures"));
}

static void fm_operation_rebuild(const char *output_dir) {
    char dump_path[FM_PATH_CAP] = "";
    char output_path[FM_PATH_CAP] = "";
    char error[512] = "";
    uint64_t written = 0;
    if (!fm_select_or_report(1, dump_path, sizeof(dump_path),
                             L"selecionar dump editado", L"Select edited dump")) return;
    puts(fm_text("[+] validando o dump antes de reconstruir", "[+] validating the dump before rebuilding"));
    if (!fm_rebuild_dump(dump_path, output_dir, output_path, sizeof(output_path), &written, error, sizeof(error))) {
        fm_print_error("o dump e invalido ou nao pode ser reconstruido",
                        "the dump is invalid or cannot be rebuilt", error);
        return;
    }
    puts(fm_text("[+] arquivo reconstruido com sucesso", "[+] file rebuilt successfully"));
    printf("%s: %s\n", fm_text("caminho gerado", "output path"), output_path);
    printf("%s: %" PRIu64 " bytes\n", fm_text("tamanho final", "final size"), written);
    puts(fm_text("o arquivo original nao foi alterado", "the original file was not changed"));
    puts(fm_text("mudancas podem quebrar checksums, assinaturas e estruturas",
                 "changes may break checksums, signatures and structures"));
    puts(fm_text("nao ha garantia de que um executavel reconstruido funcione",
                 "a rebuilt executable is not guaranteed to work"));
}

static void fm_operation_analyze(void) {
    char path[FM_PATH_CAP] = "";
    char error[512] = "";
    fm_analysis_t analysis;
    size_t index;
    if (!fm_select_or_report(0, path, sizeof(path),
                             L"selecionar arquivo para analisar", L"Select file to inspect")) return;
    if (!fm_analyze_file(path, &analysis, error, sizeof(error))) {
        fm_print_error("nao foi possivel analisar o arquivo", "could not inspect the file", error);
        return;
    }
    puts(fm_text("[+] analise concluida", "[+] inspection completed"));
    printf("%s: %s\n", fm_text("arquivo", "file"), path);
    printf("%s: %" PRIu64 " bytes\n", fm_text("tamanho", "size"), analysis.size);
    printf("%s: %s\n", fm_text("tipo provavel", "probable type"), fm_type_label(analysis.type));
    printf("%s: %" PRIu64 "\n", fm_text("bytes nulos", "null bytes"), analysis.null_bytes);
    printf("%s: %" PRIu64 "\n", fm_text("bytes imprimiveis", "printable bytes"), analysis.printable_bytes);
    printf("%s: %" PRIu64 "\n",
           fm_text("sequencias ASCII (minimo 4 caracteres)", "ASCII sequences (minimum 4 characters)"),
           analysis.ascii_sequences);
    for (index = 0; index < analysis.ascii_string_count; ++index)
        printf("%s: %s\n", fm_text("texto ASCII provavel", "probable ASCII string"), analysis.ascii_strings[index]);
    if (analysis.ascii_sequences > analysis.ascii_string_count)
        puts(fm_text("a lista de textos foi limitada para manter a saida legivel",
                     "the string list was limited to keep output readable"));
    if (analysis.pe_valid) {
        printf("%s: 0x%08" PRIx32 "\n", fm_text("cabecalho PE", "PE header"), analysis.pe_offset);
        printf("%s: %s\n", fm_text("formato PE", "PE format"), analysis.pe_magic == 0x20b ? "PE32+" : "PE32");
        printf("%s: %u\n", fm_text("secoes", "sections"), (unsigned int)analysis.pe_sections);
        for (index = 0; index < analysis.pe_sections; ++index) {
            const fm_pe_section_t *section = &analysis.sections[index];
            const char *kind;
            if (section->characteristics & 0x20U)
                kind = fm_text("codigo", "code");
            else if (section->characteristics & 0x40U)
                kind = fm_text("dados inicializados", "initialized data");
            else if (section->characteristics & 0x80U)
                kind = fm_text("dados nao inicializados", "uninitialized data");
            else
                kind = fm_text("outro", "other");
            printf("%s %s: %s 0x%08" PRIx32 ", %s %" PRIu32 ", %s %s\n",
                   fm_text("secao", "section"), section->name,
                   fm_text("deslocamento", "offset"), section->raw_offset,
                   fm_text("tamanho", "size"), section->raw_size,
                   fm_text("tipo", "kind"), kind);
        }
        puts(fm_text("a classificacao de secoes e heuristica", "section classification is heuristic"));
    }
    puts(fm_text("a identificacao e heuristica e nao explica todos os bytes",
                 "identification is heuristic and does not explain every byte"));
}

static void fm_operation_compare(const char *output_dir) {
    char left_path[FM_PATH_CAP] = "";
    char right_path[FM_PATH_CAP] = "";
    char error[512] = "";
    fm_compare_result_t result;
    size_t index;
    if (!fm_select_or_report(0, left_path, sizeof(left_path),
                             L"selecionar primeiro arquivo", L"Select first file")) return;
    if (!fm_select_or_report(0, right_path, sizeof(right_path),
                             L"selecionar segundo arquivo", L"Select second file")) return;
    if (!fm_compare_files(left_path, right_path, output_dir, &result, error, sizeof(error))) {
        fm_print_error("nao foi possivel comparar os arquivos", "could not compare the files", error);
        return;
    }
    printf("%s: %" PRIu64 " bytes\n", fm_text("primeiro arquivo", "first file"), result.left_size);
    printf("%s: %" PRIu64 " bytes\n", fm_text("segundo arquivo", "second file"), result.right_size);
    puts(result.identical ? fm_text("[+] os arquivos sao identicos", "[+] the files are identical") :
                            fm_text("[!] os arquivos sao diferentes", "[!] the files are different"));
    printf("%s: %" PRIu64 "\n", fm_text("posicoes diferentes", "differing positions"), result.different_positions);
    printf("%s: %zu\n", fm_text("diferencas exibidas", "differences shown"), result.shown_count);
    for (index = 0; index < result.shown_count; ++index) {
        const fm_difference_t *difference = &result.shown[index];
        if (difference->left_value < 0)
            printf("%s 0x%08" PRIx64 ": %s --, %s %02x\n",
                   fm_text("deslocamento", "offset"), difference->offset,
                   fm_text("esquerda", "left"), fm_text("direita", "right"),
                   (unsigned int)difference->right_value);
        else if (difference->right_value < 0)
            printf("%s 0x%08" PRIx64 ": %s %02x, %s --\n",
                   fm_text("deslocamento", "offset"), difference->offset,
                   fm_text("esquerda", "left"), (unsigned int)difference->left_value,
                   fm_text("direita", "right"));
        else
            printf("%s 0x%08" PRIx64 ": %s %02x, %s %02x\n",
                   fm_text("deslocamento", "offset"), difference->offset,
                   fm_text("esquerda", "left"), (unsigned int)difference->left_value,
                   fm_text("direita", "right"), (unsigned int)difference->right_value);
    }
    if (result.left_size != result.right_size)
        puts(fm_text("um dos arquivos tem bytes extras no final", "one file has extra trailing bytes"));
    if (result.report_path[0] != '\0')
        printf("%s (%u): %s\n", fm_text("relatorio, limite de diferencas", "report, difference limit"),
               FM_COMPARE_REPORT_LIMIT, result.report_path);
}

int main(void) {
    char output_dir[FM_PATH_CAP] = "";
    char error[512] = "";
    long choice;
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    srand((unsigned int)time(NULL));
    if (!fm_choose_language()) return 0;
    if (!fm_get_output_dir(output_dir, sizeof(output_dir), error, sizeof(error))) {
        fm_print_error("nao foi possivel preparar a pasta de saida",
                        "could not prepare the output directory", error);
        return 2;
    }
    printf("%s: %s\n", fm_text("pasta de saida", "output directory"), output_dir);
    for (;;) {
        fm_show_menu();
        if (!fm_read_choice(fm_text("escolha uma opcao: ", "choose an option: "), 0, 5, &choice)) {
            puts(fm_text("\nencerrando o programa", "\nclosing the program"));
            break;
        }
        switch (choice) {
            case 1: fm_operation_export(output_dir); break;
            case 2: fm_operation_rebuild(output_dir); break;
            case 3: fm_operation_analyze(); break;
            case 4: fm_operation_compare(output_dir); break;
            case 5:
                if (fm_open_output_directory(output_dir))
                    puts(fm_text("[+] pasta de saida aberta", "[+] output directory opened"));
                else
                    fm_print_error("nao foi possivel abrir a pasta de saida",
                                   "could not open the output directory", output_dir);
                break;
            case 0:
                puts(fm_text("encerrando o programa", "closing the program"));
                return 0;
        }
    }
    return 0;
}
