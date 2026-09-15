#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "tradutorlinux/runtime/unwind.hpp"
#include "tradutorlinux/runtime/cxx_eh.hpp"

#if defined(__GNUC__) || defined(__clang__)
#define TL_CRT_MSABI __attribute__((ms_abi))
#else
#error "TL_CRT_MSABI requer GCC ou Clang em x86-64"
#endif

namespace tradutorlinux {

// Layout Microsoft x64 do _iobuf (48 bytes). Campos em offsets fixos:
// _ptr@0x00, _cnt@0x08, _base@0x10, _flag@0x18, _file@0x1c, _charbuf@0x20,
// _bufsiz@0x24, _tmpfname@0x28. O convidado lê/escreve _flag e _file.
struct GuestFile {
    char* ptr{nullptr};
    int count{0};
    char* base{nullptr};
    int flag{0};
    int file{-1};
    int charbuf{-1};
    int bufsiz{0};
    char* tmpfname{nullptr};
};
static_assert(sizeof(GuestFile) == 48);

// va_list do ABI Microsoft x64: char* apontando para o primeiro slot de
// argumento; os slots são consecutivos e têm 8 bytes.
using GuestVaList = char*;

// Fronteira de ABI do msvcrt mínimo: funções hospedeiras chamadas por código
// PE32+ x86-64. Usam a convenção Microsoft x64 (TL_CRT_MSABI) e não propagam
// exceções C++.
using GuestBeginThreadProc = unsigned (TL_CRT_MSABI *)(void*);

extern "C" {

TL_CRT_MSABI int tl___getmainargs(int* argc, char*** argv, char*** envp, int* glob,
                                  void* startup_info) noexcept;
TL_CRT_MSABI void tl___initterm(void (**start)(void), void (**end)(void)) noexcept;
TL_CRT_MSABI void tl___set_app_type(int app_type) noexcept;
TL_CRT_MSABI void tl___setusermatherr(void (*handler)(void)) noexcept;
TL_CRT_MSABI void tl__amsg_exit(int error_code) noexcept;
TL_CRT_MSABI void tl__cexit() noexcept;
TL_CRT_MSABI int* tl__errno() noexcept;
TL_CRT_MSABI void tl__lock(GuestFile* file) noexcept;
TL_CRT_MSABI void tl__unlock(GuestFile* file) noexcept;
TL_CRT_MSABI void tl_abort() noexcept;
TL_CRT_MSABI int tl_atexit(void (*handler)(void)) noexcept;
TL_CRT_MSABI int tl_atoi(const char* str) noexcept;
TL_CRT_MSABI void* tl_calloc(std::size_t count, std::size_t size) noexcept;
TL_CRT_MSABI void tl_exit(int exit_code) noexcept;
TL_CRT_MSABI int tl_fclose(GuestFile* file) noexcept;
TL_CRT_MSABI int tl_feof(const GuestFile* file) noexcept;
TL_CRT_MSABI int tl_ferror(const GuestFile* file) noexcept;
TL_CRT_MSABI int tl_fflush(GuestFile* file) noexcept;
TL_CRT_MSABI GuestFile* tl_fopen(const char* path, const char* mode) noexcept;
TL_CRT_MSABI int tl_fprintf(GuestFile* file, const char* format, ...) noexcept;
TL_CRT_MSABI int tl_fputc(int character, GuestFile* file) noexcept;
TL_CRT_MSABI int tl_fputs(const char* text, GuestFile* file) noexcept;
TL_CRT_MSABI void tl_free(void* pointer) noexcept;
TL_CRT_MSABI int tl_fseek(GuestFile* file, long offset, int origin) noexcept;
TL_CRT_MSABI long tl_ftell(GuestFile* file) noexcept;
TL_CRT_MSABI std::size_t tl_fwrite(const void* buffer, std::size_t size, std::size_t count,
                                   GuestFile* file) noexcept;
TL_CRT_MSABI int tl_getc(GuestFile* file) noexcept;
TL_CRT_MSABI char* tl_getenv(const char* name) noexcept;
TL_CRT_MSABI int tl_isalnum(int character) noexcept;
TL_CRT_MSABI void* tl_localeconv() noexcept;
TL_CRT_MSABI void* tl_malloc(std::size_t size) noexcept;
TL_CRT_MSABI void* tl_memcpy(void* destination, const void* source, std::size_t count) noexcept;
TL_CRT_MSABI void* tl_memset(void* destination, int value, std::size_t count) noexcept;
TL_CRT_MSABI void tl_perror(const char* message) noexcept;
TL_CRT_MSABI int tl_putc(int character, GuestFile* file) noexcept;
TL_CRT_MSABI void tl_rewind(GuestFile* file) noexcept;
TL_CRT_MSABI void* tl_signal(int signal_number, void (*handler)(int)) noexcept;
TL_CRT_MSABI int tl_strcmp(const char* left, const char* right) noexcept;
TL_CRT_MSABI char* tl_strcpy(char* destination, const char* source) noexcept;
TL_CRT_MSABI char* tl_strerror(int error_number) noexcept;
TL_CRT_MSABI std::size_t tl_strlen(const char* text) noexcept;
TL_CRT_MSABI int tl_strncmp(const char* left, const char* right, std::size_t count) noexcept;
TL_CRT_MSABI long tl_strtol(const char* text, char** end_pointer, int base) noexcept;
TL_CRT_MSABI unsigned long tl_strtoul(const char* text, char** end_pointer, int base) noexcept;
TL_CRT_MSABI int tl_toupper(int character) noexcept;
TL_CRT_MSABI int tl_vfprintf(GuestFile* file, const char* format, GuestVaList arguments) noexcept;
TL_CRT_MSABI std::size_t tl_wcslen(const std::uint16_t* text) noexcept;
TL_CRT_MSABI GuestFile* tl___iob_func() noexcept;
TL_CRT_MSABI GuestFile* tl__fdopen(int file_descriptor, const char* mode) noexcept;
TL_CRT_MSABI int tl__fileno(const GuestFile* file) noexcept;
TL_CRT_MSABI int tl__isatty(int file_descriptor) noexcept;
TL_CRT_MSABI int tl__open(const char* path, int oflag, ...) noexcept;
TL_CRT_MSABI int tl__setmode(int file_descriptor, int mode) noexcept;
TL_CRT_MSABI unsigned int tl___lc_codepage_func() noexcept;
TL_CRT_MSABI int tl___mb_cur_max_func() noexcept;
TL_CRT_MSABI int tl__ismbblead(unsigned int character) noexcept;
TL_CRT_MSABI std::int32_t tl___C_specific_handler(
    runtime::ExceptionRecordAmd64* exception_record, void* establisher_frame,
    runtime::ContextAmd64* context_record,
    runtime::DispatcherContextAmd64* dispatcher_context) noexcept;
TL_CRT_MSABI int tl_fgetc(GuestFile* file) noexcept;
TL_CRT_MSABI std::size_t tl_fread(void* buffer, std::size_t size, std::size_t count,
                                   GuestFile* file) noexcept;
TL_CRT_MSABI int tl_ungetc(int character, GuestFile* file) noexcept;
TL_CRT_MSABI char* tl_strncpy(char* destination, const char* source, std::size_t count) noexcept;
TL_CRT_MSABI char* tl_strstr(const char* haystack, const char* needle) noexcept;
TL_CRT_MSABI int tl_isspace(int character) noexcept;
TL_CRT_MSABI char* tl_strcat(char* destination, const char* source) noexcept;
TL_CRT_MSABI void* tl_memmove(void* destination, const void* source, std::size_t count) noexcept;
TL_CRT_MSABI int tl_remove(const char* path) noexcept;
TL_CRT_MSABI int tl__stat64(const char* path, void* stat_buffer) noexcept;
TL_CRT_MSABI void* tl_realloc(void* pointer, std::size_t size) noexcept;
TL_CRT_MSABI char* tl_setlocale(int category, const char* locale) noexcept;
TL_CRT_MSABI char* tl_strchr(const char* text, int character) noexcept;
TL_CRT_MSABI char* tl_strrchr(const char* text, int character) noexcept;
TL_CRT_MSABI int tl__stricmp(const char* left, const char* right) noexcept;
TL_CRT_MSABI char* tl__strdup(const char* text) noexcept;
TL_CRT_MSABI int tl__umask(int mask) noexcept;
TL_CRT_MSABI int tl__chmod(const char* path, int mode) noexcept;
TL_CRT_MSABI int tl__utime64(const char* path, const void* times) noexcept;
TL_CRT_MSABI GuestFile* tl__wfopen(const std::uint16_t* path, const std::uint16_t* mode) noexcept;
TL_CRT_MSABI int tl__wstat64(const std::uint16_t* path, void* stat_buffer) noexcept;
TL_CRT_MSABI int tl__wrename(const std::uint16_t* old_path, const std::uint16_t* new_path) noexcept;
TL_CRT_MSABI int tl__wunlink(const std::uint16_t* path) noexcept;
TL_CRT_MSABI std::uint16_t* tl__wcsdup(const std::uint16_t* text) noexcept;
TL_CRT_MSABI std::uint16_t* tl_wcschr(const std::uint16_t* text, std::uint16_t character) noexcept;
TL_CRT_MSABI std::uint16_t* tl_wcsrchr(const std::uint16_t* text, std::uint16_t character) noexcept;
TL_CRT_MSABI std::uint16_t* tl_wcsncat(std::uint16_t* destination, const std::uint16_t* source,
                                       std::size_t count) noexcept;
TL_CRT_MSABI std::uint16_t* tl_wcsncpy(std::uint16_t* destination, const std::uint16_t* source,
                                       std::size_t count) noexcept;
TL_CRT_MSABI std::size_t tl_mbstowcs(std::uint16_t* destination, const char* source,
                                     std::size_t count) noexcept;
TL_CRT_MSABI std::size_t tl_wcstombs(char* destination, const std::uint16_t* source,
                                    std::size_t count) noexcept;
TL_CRT_MSABI int tl_fwprintf(GuestFile* file, const std::uint16_t* format, ...) noexcept;
TL_CRT_MSABI int tl_fputwc(std::uint16_t character, GuestFile* file) noexcept;
TL_CRT_MSABI std::int64_t tl__time64(std::int64_t* value) noexcept;
TL_CRT_MSABI void* tl__localtime64(const std::int64_t* value) noexcept;
TL_CRT_MSABI std::size_t tl_strftime(char* buffer, std::size_t capacity, const char* format,
                                     const void* time_value) noexcept;
TL_CRT_MSABI char* tl__strlwr(char* text) noexcept;
TL_CRT_MSABI void tl__c_exit() noexcept;
TL_CRT_MSABI void tl__exit(int exit_code) noexcept;
TL_CRT_MSABI int tl__XcptFilter(unsigned long xcpt, void* pinfo) noexcept;
TL_CRT_MSABI void* tl___dllonexit(void (*func)(void), void** pbegin, void** pend) noexcept;
TL_CRT_MSABI std::uintptr_t tl__beginthreadex(void* security, unsigned stack_size,
                                              GuestBeginThreadProc start_address, void* arg_list,
                                              unsigned init_flag, unsigned* thread_id) noexcept;
TL_CRT_MSABI int tl_memcmp(const void* ptr1, const void* ptr2, std::size_t num) noexcept;
TL_CRT_MSABI int tl_wcscmp(const std::uint16_t* string1, const std::uint16_t* string2) noexcept;
TL_CRT_MSABI std::uint16_t* tl_wcsstr(const std::uint16_t* string, const std::uint16_t* str_char_set) noexcept;
TL_CRT_MSABI int tl___CxxFrameHandler(void* rec, void* frame, void* context, void* disp) noexcept;
TL_CRT_MSABI int tl___CxxFrameHandler3(void* rec, void* frame, void* context, void* disp) noexcept;
TL_CRT_MSABI int tl___GSHandlerCheck_EH4(void* rec, void* frame, void* context, void* disp) noexcept;
TL_CRT_MSABI void tl__CxxThrowException(void* pexcept, void* pthrow_info) noexcept;
TL_CRT_MSABI void tl__purecall() noexcept;
TL_CRT_MSABI void tl_terminate() noexcept;
TL_CRT_MSABI int tl_rand() noexcept;
TL_CRT_MSABI void tl_srand(unsigned int seed) noexcept;

// Dados exportados por msvcrt.dll: células graváveis do hospedeiro cujos
// endereços são gravados nos slots da IAT (imports-dados __initenv, _commode
// e _fmode).
extern char** g_guest_initenv;
extern int g_guest_commode;
extern int g_guest_fmode;
extern char* g_guest_acmdln;
extern GuestFile g_guest_iob[3];

}  // extern "C"

// Argumentos do convidado (argv[0] é o caminho do executável). Definido pelo
// CLI antes da execução; o convidado lê em __getmainargs no processo filho.
void msvcrt_set_guest_command_line(std::vector<std::string> arguments);

const std::vector<std::string>& msvcrt_get_guest_arguments() noexcept;

}  // namespace tradutorlinux
