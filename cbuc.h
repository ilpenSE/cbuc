#ifndef CBUC_H
#define CBUC_H

#ifdef CBUC_STATIC
#define CBUCDEF static
#else
#ifdef __cplusplus
#define CBUCDEF extern "C"
#else
#define CBUCDEF extern
#endif // __cplusplus
#endif

#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

// You can change these to align with your environment
// or when you do cross-compilation
static const char *MINGW_COMPILER = "x86_64-w64-mingw32-gcc";
static const char *GCC_COMPILER = "gcc";
static const char *CLANG_COMPILER = "clang";
static const char *MSVC_COMPILER = "cl";
static const char *INTEL_COMPILER = "icx";

#ifdef _WIN32
#define NATIVE_COMPILER "cl"
#else
#define NATIVE_COMPILER "cc"
#endif

// External commands
typedef struct Cmd {
  const char **items;
  size_t count;
  size_t capacity;
} Cmd;

typedef struct CmdRunOpts {
  bool log;
  bool reset;
} CmdRunOpts;

// Append a flag to Cmd directly
#define cmd_append da_append

// Run builded Cmd structure
#define cmd_run(cmd, ...) \
  cmd_run_impl((cmd), (CmdRunOpts){.log=true, .reset=true, __VA_ARGS__})
CBUCDEF bool cmd_run_impl(Cmd *cmd, CmdRunOpts opts);

// Reset whole Cmd array (Does not free it's elements)
CBUCDEF void cmd_reset(Cmd *cmd);

// Appends NATIVE_COMPILER to Cmd
CBUCDEF Cmd *cmd_native_compiler(Cmd *cmd);
// Appends a compiler which is compiled this program to Cmd
CBUCDEF Cmd *cmd_self_compiler(Cmd *cmd);
// Appends output flag to Cmd (-o or /Fe)
CBUCDEF Cmd *cmd_output(Cmd *cmd, const char *file_name);

// Appends input(s) to Cmd
#define cmd_input(cmd, ...) \
  cmd_input_impl((cmd), sizeof((const char *[]){__VA_ARGS__})/sizeof(*(const char *[]){__VA_ARGS__}), __VA_ARGS__)
CBUCDEF Cmd *cmd_input_impl(Cmd *cmd, int n, ...);

// Rebuild urself
/*
  You use it like this:
  ```c
  int main(int argc, char **argv)
  {
    GO_REBUILD_URSELF(argc, argv);
  }
  ```

  This thing detects changes by comparing last modification times of your .c file and binary
  It saves the binary by renaming it to "<bin>.old" then it re-compiles itself using native compiler
  Finally, it runs new binary with flags you provided before and quits.
*/
#define GO_REBUILD_URSELF(argc, argv, ...) \
  if (!cbuc_rebuild_urself_impl((argc), (argv), __FILE__, sizeof((const char *[]){__VA_ARGS__})/sizeof(*(const char *[]){__VA_ARGS__}), ##__VA_ARGS__)) \
  { \
    fprintf(stderr, "ERROR: Rebuilding urself failed!!\n"); return 1; \
  }

CBUCDEF bool cbuc_rebuild_urself_impl(int argc, char **argv, const char *file_name, int n, ...);

// String types
typedef struct {
  union {
    char *items;
    char *data;
  };
  size_t count;
  size_t capacity;
} String;

typedef struct {
  union {
    const char *items;
    const char *data;
  };
  size_t count;
} StringView;

typedef struct {
  union {
    wchar_t *items;
    wchar_t *data;
  };
  size_t count;
  size_t capacity;
} WString;

// Dynamic arrays

// Reserve "extra" amount of elements, if current count + extra doesn't fit, reallocate some space
// void da_reserve(array<T> *arr, size_t extra);
#define da_reserve(arr, extra)                                                     \
  do {                                                                             \
    if ((arr)->count + extra >= (arr)->capacity) {                                 \
      size_t new_cap;                                                              \
      if ((arr)->capacity == 0) new_cap = 32;                                      \
      else new_cap = (arr)->capacity;                                              \
      while (new_cap < (arr)->count + extra) new_cap *= 2;                         \
      void *tmp = realloc((arr)->items, new_cap * sizeof(*(arr)->items));          \
      if (!tmp) { assert(false && "program terminated because of realloc() failed! \n"); }          \
      (arr)->items = tmp;                                                          \
      (arr)->capacity = new_cap;                                                   \
    }                                                                              \
  } while (0)

// Append an element to the dynamic array
// void da_append(array<T> *arr, T item);
#define da_append(arr, item)               \
  do {                                     \
    da_reserve((arr), 1);                  \
    (arr)->items[(arr)->count++] = (item); \
  } while (0)

// Merge `arr2` to `arr1` (Doesn't mutate `arr2`)
// void da_append(array<T> *arr1, array<T> *arr2);
#define da_merge(arr1, arr2) \
  da_concat((arr1), (arr2)->items, (arr2)->count)

// Concat a buffer pointed by `ptr` and sized by `len` to `arr`
// void da_append(array<T> *arr, T *ptr, size_t len);
#define da_concat(arr, ptr, len)                   \
  do {                                             \
    da_reserve((arr), len);                        \
    memcpy((arr)->items + (arr)->count, ptr, len); \
    (arr)->count += (len);                         \
  } while (0)

// Free the items and zero `arr`
// void da_free(array<T> *arr)
#define da_free(arr)     \
  do {                   \
    free((arr)->items);  \
    (arr)->items = NULL; \
    (arr)->count = 0;    \
    (arr)->capacity = 0; \
  } while (0)

#if defined(CBUC) || defined(CBUC_IMPL) || defined(CBUC_IMPLEMENTATION)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/time.h>
#endif

#ifdef _WIN32
static WString _cbuc_win32_to_wstring(const char *buffer, size_t size)
{
  WString wstr = {0};
  int utf16_len = MultiByteToWideChar(CP_UTF8, 0, buffer, size, NULL, 0);
  da_reserve(&wstr, utf16_len + 1);
  MultiByteToWideChar(CP_UTF8, 0, buffer, size, wstr.data, utf16_len);
  wstr.count += utf16_len;
  da_append(&wstr, u'\0');
  return wstr;
}

int64_t _cbuc_win32_get_file_mtime_in_seconds(const char *file_name, size_t size)
{
  FILETIME ft;
  WString wfile_name = _cbuc_win32_to_wstring(file_name, size);
  HANDLE handle = CreateFileW(wfile_name.data, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
  if (handle == INVALID_HANDLE_VALUE) return -1;
  if (!GetFileTime(handle, NULL, NULL, &ft)) return -1;
  CloseHandle(handle);
  da_free(&wfile_name);
  ULARGE_INTEGER u;
  u.LowPart  = ft.dwLowDateTime;
  u.HighPart = ft.dwHighDateTime;
  return u.QuadPart / 10000000ULL;
}
#endif

CBUCDEF bool cbuc_rebuild_urself_impl(int argc, char **argv, const char *file_name, int n, ...)
{
  (void)argc;
  char *bin_name = argv[0];
  size_t bin_name_len = strlen(bin_name);

  // Get last modification time of file (script) and binary
  // If file's mod time > binary, that means it's time to rebuild!
#ifdef _WIN32
  int64_t bin_mtime  = _cbuc_win32_get_file_mtime_in_seconds(bin_name, bin_name_len);
  int64_t file_mtime = _cbuc_win32_get_file_mtime_in_seconds(file_name, strlen(file_name));
  if (file_mtime == -1) {
    fprintf(stderr, "ERROR: could not get modification date of '%s'! Error %d\n",
            file_name, GetLastError());
    return false;
  }
  if (bin_mtime == -1) {
    fprintf(stderr, "ERROR: could not get modification date of '%.*s'! Error %d\n",
            (int)bin_name_len, bin_name, GetLastError());
    return false;
  }

  if (file_mtime < bin_mtime) return true;

#else
  struct stat binary_st, file_st;
  if (stat(bin_name, &binary_st) != 0) {
    fprintf(stderr, "ERROR: could not stat '%.*s': %s\n",
            (int)bin_name_len, bin_name, strerror(errno));
    return false;
  }
  if (stat(file_name, &file_st) != 0) {
    fprintf(stderr, "ERROR: could not stat '%s': %s\n",
            file_name, strerror(errno));
    return false;
  }

  if (file_st.st_mtime < binary_st.st_mtime) return true;
#endif

  String old_bin_name = {0};
  da_reserve(&old_bin_name, bin_name_len + 6);
  da_concat(&old_bin_name, bin_name, bin_name_len);
  da_concat(&old_bin_name, ".old", 4);
  da_append(&old_bin_name, '\0');

  // Save old binary
  printf("Renaming: '%.*s' -> '%.*s'\n", (int)bin_name_len, bin_name,
         (int)old_bin_name.count, old_bin_name.data);

#ifdef _WIN32
  WString dst = _cbuc_win32_to_wstring(old_bin_name.data, old_bin_name.count);
  WString src = _cbuc_win32_to_wstring(bin_name, bin_name_len);
  ReplaceFileW(dst.data, src.data, NULL, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL);
  da_free(&dst); da_free(&src);
#else
  if (rename(bin_name, old_bin_name.data) != 0) {
    fprintf(stderr, "ERROR: Rename failed: %s\n", strerror(errno));
    return false;
  }
#endif

  // Compile
  Cmd cmd = {0};
  cmd_self_compiler(&cmd);
  cmd_output(&cmd, bin_name);
  cmd_input(&cmd, file_name);

  va_list args;
  va_start(args, n);
  for (int i = 0; i < n; i++) {
    const char *arg = va_arg(args, const char *);
    cmd_append(&cmd, arg);
  }
  va_end(args);

  if (!cmd_run(&cmd)) return false;

  // Run new program
#ifdef _WIN32
  da_concat(&cmd, argv, argc);
  if (!cmd_run(&cmd)) return false;
  da_free(&cmd);
  exit(0);
#else
  da_free(&cmd);
  execvp(argv[0], (char * const *)argv);
  fprintf(stderr, "ERROR: execvp failed: %s\n", strerror(errno));
#endif
  return false;
}

CBUCDEF Cmd *cmd_native_compiler(Cmd *cmd)
{
  cmd_append(cmd, NATIVE_COMPILER);
  return cmd;
}

CBUCDEF Cmd *cmd_self_compiler(Cmd *cmd)
{
#ifdef __clang__ // LLVM's Clang
  cmd_append(cmd, CLANG_COMPILER);
#elif defined(__MINGW32__) // MinGW
  cmd_append(cmd, MINGW_COMPILER);
#elif defined(__GNUC__) // GNU Compiler Collection
  cmd_append(cmd, GCC_COMPILER);
#elif defined(_MSC_VER) // Microsoft's C/C++ compiler (MSVC)
  cmd_append(cmd, MSVC_COMPILER);
#elif defined(__INTEL_LLVM_COMPILER) // Intel Modern LLVM-based C/C++ compiler
  cmd_append(cmd, INTEL_COMPILER);
#endif
  return cmd;
}

CBUCDEF Cmd *cmd_output(Cmd *cmd, const char *file_name)
{
  if (strncmp(cmd->items[0], "cl", 2) == 0) {
    cmd_append(cmd, "/Fe");
  } else {
    cmd_append(cmd, "-o");
  }
  cmd_append(cmd, file_name);
  return cmd;
}

CBUCDEF Cmd *cmd_input_impl(Cmd *cmd, int n, ...)
{
  va_list args;
  va_start(args, n);
  for (int i = 0; i < n; i++) {
    const char *arg = va_arg(args, const char *);
    cmd_append(cmd, arg);
  }
  va_end(args);
  return cmd;
}

#ifdef _WIN32
CBUCDEF bool cmd_run_impl(Cmd *cmd, CmdRunOpts opts)
{
  bool ok = false;
  STARTUPINFOW siStartupInfo = {0};
  siStartupInfo.cb = sizeof(siStartupInfo);
  PROCESS_INFORMATION siProcessInformation = {0};

  String cmdline_utf8 = {0};
  for (size_t i = 0; i < cmd->count; i++) {
    const char *arg = cmd->items[i];
    size_t arg_len = strlen(arg);
    if (i == 0) da_append(&cmdline_utf8, '"');
    da_concat(&cmdline_utf8, arg, arg_len);
    if (i == 0) da_append(&cmdline_utf8, '"');
    da_append(&cmdline_utf8, ' ');
  }
  da_append(&cmdline_utf8, '\0');

  if (opts.log) printf("Command: %.*s\n", (int)cmdline_utf8.count, cmdline_utf8.data);

  WString cmdline_utf16 = {0};
  int utf16_len = MultiByteToWideChar(CP_UTF8, 0, cmdline_utf8.data, cmdline_utf8.count, NULL, 0);
  da_reserve(&cmdline_utf16, utf16_len + 1);
  MultiByteToWideChar(CP_UTF8, 0, cmdline_utf8.data, cmdline_utf8.count, cmdline_utf16.data, utf16_len);
  da_append(&cmdline_utf16, u'\0');

  BOOL ret = CreateProcessW(NULL, cmdline_utf16.data,
                            NULL, NULL, false,
                            0, NULL, NULL,
                            &siStartupInfo, &siProcessInformation);
  da_free(&cmdline_utf8);
  da_free(&cmdline_utf16);
  if (!ret) {
    fprintf(stderr, "ERROR: CreateProcessW failed: %d\n", GetLastError());
    goto clean;
  }

  CloseHandle(siProcessInformation.hThread);
  HANDLE hProcess = siProcessInformation.hProcess;

  if (WaitForSingleObject(hProcess, INFINITE) != WAIT_OBJECT_0) {
    fprintf(stderr, "ERROR: WaitForSingleObject failed: %d\n", GetLastError());
    goto clean;
  }

  DWORD status;
  if (!GetExitCodeProcess(hProcess, &status)) {
    fprintf(stderr, "ERROR: GetExitCodeProcess failed: %d\n", GetLastError());
    goto clean;
  }

  if (status != 0) {
    fprintf(stderr, "ERROR: Command failed with code %d.\n", status);
    goto clean;
  }

  ok = true;
clean:
  if (opts.reset) cmd->count = 0;
  return ok;
}

#else // POSIX
CBUCDEF bool cmd_run_impl(Cmd *cmd, CmdRunOpts opts)
{
  bool ok = false;
  if (opts.log) {
    printf("Command:");
    for (size_t i = 0; i < cmd->count; i++) {
      printf(" %s", cmd->items[i]);
    }
    printf("\n");
  }

  pid_t pid = fork();
  if (pid > 0) {
    // parent
    int status;
    if (waitpid(pid, &status, 0) != pid) {
      if (opts.log) fprintf(stderr, "ERROR: waitpid failed: %s\n", strerror(errno));
      goto clean;
    }
    if (status != 0) {
      if (opts.log) fprintf(stderr, "ERROR: Command failed with code %d.\n", status);
      goto clean;
    }
  } else if (pid == 0) {
    // child
    da_append(cmd, NULL);
    execvp(cmd->items[0], (char * const*)cmd->items);
    if (opts.log) fprintf(stderr, "ERROR: execvp failed: %s\n", strerror(errno));
    goto clean;
  } else {
    if (opts.log) fprintf(stderr, "ERROR: fork failed: %s\n", strerror(errno));
    goto clean;
  }

  ok = true;
clean:
  if (opts.reset) cmd->count = 0;
  return ok;
}
#endif

CBUCDEF void cmd_reset(Cmd *cmd)
{
  if (!cmd) return;
  cmd->count = 0;
}

#endif // implementation macros

#endif // CBUC_H
