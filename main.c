#define CBUC
#include "cbuc.h"

int main(int argc, char **argv)
{
  GO_REBUILD_URSELF(argc, argv);
  Cmd cmd = {0};

  cmd_self_compiler(&cmd);
  cmd_input(&cmd, "main.c");
  cmd_output(&cmd, "main_generated");
  if (!cmd_run(&cmd)) return 1;
  return 0;
}
