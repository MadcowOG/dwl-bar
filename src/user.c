#include "user.h"
#include "util.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

void spawn(struct Monitor *monitor, const union Arg *arg) {
  if (fork() != 0)
    return;

  char* const* argv = arg->v;
  setsid();
  execvp(argv[0], argv);
  fprintf(stderr, "dwl-bar: execvp %s", argv[0]);
  perror(" failed\n");
  exit(1);
}
