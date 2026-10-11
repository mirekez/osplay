#include <kernel/path.hpp>

#include <support/test.hpp>

namespace {

using namespace osplay;

[[nodiscard]] bool text_is(const char* actual, const char* expected) {
  while (*actual == *expected) {
    if (*actual == '\0') {
      return true;
    }
    ++actual;
    ++expected;
  }
  return false;
}

void check(osplay::test::Suite& suite, const char* cwd, const char* input,
           const char* expected) {
  char output[path::capacity]{};
  OSPLAY_CHECK(suite,
              path::canonicalize(cwd, input, output, sizeof(output)));
  OSPLAY_CHECK(suite, text_is(output, expected));
}

}  // namespace

int main() {
  osplay::test::Suite suite{"kernel/path"};
  OSPLAY_CHECK(suite, path::numbered_suffix("/dev/pts/0", "/dev/pts/", 4) == 0);
  OSPLAY_CHECK(suite, path::numbered_suffix("/dev/pts/3", "/dev/pts/", 4) == 3);
  OSPLAY_CHECK(suite, path::numbered_suffix("/dev/pts/4", "/dev/pts/", 4) ==
                         path::invalid_number);
  OSPLAY_CHECK(suite,
              path::numbered_suffix("/proc/self/fd/15", "/proc/self/fd/", 16) ==
                  15);
  OSPLAY_CHECK(suite,
              path::numbered_suffix("/proc/self/fd/16", "/proc/self/fd/", 16) ==
                  path::invalid_number);
  OSPLAY_CHECK(suite,
              path::numbered_suffix("/proc/self/fd/x", "/proc/self/fd/", 16) ==
                  path::invalid_number);
  check(suite, "/", "/", "/");
  check(suite, "/", "bin", "/bin");
  check(suite, "/bin", "./stress-ng", "/bin/stress-ng");
  check(suite, "/usr/share", "../bin/./tool", "/usr/bin/tool");
  check(suite, "/bin", "../../etc//issue", "/etc/issue");
  check(suite, "/ignored", "//bin///busybox", "/bin/busybox");

  char output[4]{};
  OSPLAY_CHECK(suite, !path::canonicalize("/", "tool", output,
                                         sizeof(output)));
  OSPLAY_CHECK(suite, !path::canonicalize("/bin", "", output,
                                         sizeof(output)));
  return suite.finish();
}
