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
#include <stdio.h>
#include <string.h>
#include <errno.h>

typedef struct Cmd {
  const char **items;
  size_t count;
  size_t capacity;
} Cmd;

typedef struct CmdRunOpts {
  bool log;
  bool reset;
} CmdRunOpts;

#define cmd_append(cmd, ...) \
  cmd_append_impl((cmd), sizeof((const char *[]){__VA_ARGS__})/sizeof(*(const char *[]){__VA_ARGS__}), __VA_ARGS__)
CBUCDEF Cmd *cmd_append_impl(Cmd *cmd, size_t count, ...);

#define cmd_run(cmd, ...) \
  cmd_run_impl((cmd), (CmdRunOpts){.log=true, .reset=true, __VA_ARGS__})
CBUCDEF bool cmd_run_impl(Cmd *cmd, CmdRunOpts opts);

CBUCDEF void cmd_reset(Cmd *cmd);

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
    wchar_t *items;
    wchar_t *data;
  };
  size_t count;
  size_t capacity;
} WString;

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

#define da_append(arr, item)               \
  do {                                     \
    da_reserve((arr), 1);                  \
    (arr)->items[(arr)->count++] = (item); \
  } while (0)

#define da_merge(arr1, arr2) \
  da_concat((arr1), (arr2)->items, (arr2)->count)

#define da_concat(arr1, ptr, len) \
  do { \
    da_reserve((arr1), len); \
    memcpy((arr1)->items + (arr1)->count, ptr, len); \
  } while (0)

#if defined(CBUC) || defined(CBUC_IMPL) || defined(CBUC_IMPLEMENTATION)

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

CBUCDEF Cmd *cmd_append_impl(Cmd *cmd, size_t count, ...)
{
  va_list args;
  va_start(args, count);
  for (size_t i = 0; i < count; i++) {
    const char *arg = va_arg(args, const char *);
    da_append(cmd, arg);
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
  int utf16_len = MultiByteToWideChar(CP_UTF8, 0, cmdline_utf8, cmdline_utf8.count, NULL, 0);
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
