#define CBUC
#include "cbuc.h"

int main(int argc, char **argv)
{
  Cmd cmd = {0};

  cmd_append(&cmd, "clang");
  cmd_append(&cmd, "-o", "main_clang");
  cmd_append(&cmd, "main.c");
  if (!cmd_run(&cmd)) return 1;
  return 0;
}
