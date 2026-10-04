#include "application.h"
#include <stdlib.h>
#include <string.h>
extern int extreme_busybox_main(int, char **);
#ifdef EXTREME_DROPBEAR
extern int extreme_dropbear_main(int, char **);
#endif
extern void extreme_libc_init(char **);
static struct ExtremeApplication launch;
static char *argv[EXTREME_ARGUMENTS + 1];
static char *envp[EXTREME_ARGUMENTS + 1];
void extreme_application_entry(void) {
  memcpy(&launch, extreme_application(), sizeof(launch));
  for (unsigned i = 0; i < launch.argc; ++i) {
    argv[i] = launch.text + launch.argv[i];
  }
  for (unsigned i = 0; i < launch.envc; ++i) {
    envp[i] = launch.text + launch.envp[i];
  }
  argv[launch.argc] = 0;
  envp[launch.envc] = 0;
  extreme_libc_init(envp);
  int result;
#ifdef EXTREME_DROPBEAR
  if (launch.image == 1) {
    result = extreme_dropbear_main(launch.argc, argv);
  } else
#endif
    result = extreme_busybox_main(launch.argc, argv);
  exit(result);
}
