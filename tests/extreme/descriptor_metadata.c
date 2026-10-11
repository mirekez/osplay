#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

static int check_descriptor(int descriptor, mode_t kind) {
  struct stat source, redirected;
  if (fstat(descriptor, &source) || (source.st_mode & S_IFMT) != kind || dup2(descriptor, 0) != 0 ||
      fstat(0, &redirected) || source.st_ino != redirected.st_ino ||
      source.st_mode != redirected.st_mode) {
    return 1;
  }
  if (fstatat(0, "", &redirected, AT_EMPTY_PATH) || source.st_ino != redirected.st_ino ||
      source.st_mode != redirected.st_mode) {
    return 2;
  }
  // The native Linux-generic ABI also exposes the legacy stat64 calls.
  uint32_t legacy[32];
  if (syscall(80, 0, legacy) || legacy[4] != source.st_mode ||
      legacy[2] != (uint32_t)source.st_ino || legacy[3] != (uint32_t)(source.st_ino >> 32)) {
    return 3;
  }
  if (syscall(79, 0, "", legacy, AT_EMPTY_PATH) || legacy[4] != source.st_mode ||
      legacy[2] != (uint32_t)source.st_ino) {
    return 4;
  }
  return 0;
}

static int check_pty_owner(const char *path, int slave, uid_t uid, gid_t gid) {
  struct stat by_path, by_fd;
  uint32_t legacy[32];
  return stat(path, &by_path) || fstat(slave, &by_fd) ||
         by_path.st_uid != uid || by_path.st_gid != gid ||
         by_fd.st_uid != uid || by_fd.st_gid != gid ||
         syscall(80, slave, legacy) || legacy[6] != uid || legacy[7] != gid;
}

static int check_pty_ownership(const char *path, int slave) {
  if (check_pty_owner(path, slave, 0, 0) || chown(path, 1000, 100) ||
      check_pty_owner(path, slave, 1000, 100) ||
      fchownat(AT_FDCWD, path, (uid_t)-1, 200, AT_SYMLINK_NOFOLLOW) ||
      check_pty_owner(path, slave, 1000, 200) ||
      fchownat(slave, "", 3000, (gid_t)-1, AT_EMPTY_PATH) ||
      check_pty_owner(path, slave, 3000, 200)) {
    return 1;
  }
  int directory = open("/dev/pts", O_RDONLY | O_DIRECTORY);
  const char *name = strrchr(path, '/') + 1;
  if (directory < 0 || fchownat(directory, name, 0, 0, 0) ||
      check_pty_owner(path, slave, 0, 0)) {
    return 2;
  }
  close(directory);
  errno = 0;
  if (fchownat(99, name, 0, 0, 0) != -1 || errno != EBADF) {
    return 3;
  }
  errno = 0;
  if (fchownat(slave, name, 0, 0, 0) != -1 || errno != ENOTDIR) {
    return 4;
  }
  errno = 0;
  if (fchownat(slave, "", 0, 0, 0) != -1 || errno != ENOENT) {
    return 5;
  }
  errno = 0;
  if (fchownat(AT_FDCWD, path, 0, 0, 0x8000) != -1 || errno != EINVAL) {
    return 6;
  }
  errno = 0;
  if (syscall(54, AT_FDCWD, 0, 0, 0, 0) != -1 || errno != EFAULT) {
    return 7;
  }
  errno = 0;
  if (chown("/etc/passwd", 123, 456) != -1 || errno != EROFS) {
    return 8;
  }
  // Dropbear resets ownership and permissions when the SSH shell exits.
  if (chown(path, 0, 0) || chmod(path, 0666) ||
      check_pty_owner(path, slave, 0, 0)) {
    return 9;
  }
  close(0);
  close(slave);
  return 0;
}

int extreme_busybox_main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  int file = open("/etc/passwd", O_RDONLY);
  if (file < 0 || check_descriptor(file, S_IFREG)) {
    return 1;
  }
  close(file);
  int descriptors[2];
  if (pipe(descriptors) || check_descriptor(descriptors[0], S_IFIFO)) {
    return 2;
  }
  close(descriptors[0]);
  close(descriptors[1]);
  int stream = socket(AF_INET, SOCK_STREAM, 0);
  if (stream < 0 || check_descriptor(stream, S_IFSOCK)) {
    return 3;
  }
  close(stream);
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  char path[64], actual[64];
  if (master < 0 || grantpt(master) || unlockpt(master) || ptsname_r(master, path, sizeof(path))) {
    return 4;
  }
  int slave = open(path, O_RDWR | O_NOCTTY);
  if (slave < 0 || check_descriptor(slave, S_IFCHR) || ttyname_r(0, actual, sizeof(actual)) ||
      strcmp(path, actual)) {
    return 5;
  }
  int ownership_error = check_pty_ownership(path, slave);
  if (ownership_error) {
    return 10 + ownership_error;
  }
  close(master);
  // If the PTY has already disappeared at cleanup, Dropbear accepts ENOENT.
  errno = 0;
  if (chown(path, 0, 0) != -1 || errno != ENOENT) {
    return 20;
  }
  struct stat ignored;
  errno = 0;
  if (fstat(99, &ignored) != -1 || errno != EBADF) {
    return 6;
  }
  static const char marker[] = "OSPLAY_DESCRIPTOR_METADATA_OK\n";
  return write(1, marker, sizeof(marker) - 1) == sizeof(marker) - 1 ? 0 : 7;
}
