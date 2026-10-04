#ifndef EXTREME_APPLICATION_H
#define EXTREME_APPLICATION_H
#define EXTREME_ARGUMENTS 64
#define EXTREME_ARGUMENT_BYTES 8192
struct ExtremeApplication {
  unsigned image;
  unsigned argc;
  unsigned envc;
  unsigned argv[EXTREME_ARGUMENTS];
  unsigned envp[EXTREME_ARGUMENTS];
  char text[EXTREME_ARGUMENT_BYTES];
};
#ifdef __cplusplus
extern "C" {
#endif
const struct ExtremeApplication *extreme_application();
void extreme_application_entry(void);
#ifdef __cplusplus
}
#endif
#endif
