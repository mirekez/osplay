#include <kernel/pseudo_filesystem.hpp>

#include <support/test.hpp>

namespace {

using Filesystem = osplay::pseudo_fs::Filesystem;
using Node = Filesystem::Node;
using Type = Filesystem::Type;

[[nodiscard]] bool contains(const char* text, const char* needle) {
  if (text == nullptr || needle == nullptr || needle[0] == '\0') {
    return false;
  }
  for (osplay::u32 start = 0; text[start] != '\0'; ++start) {
    osplay::u32 index = 0;
    while (needle[index] != '\0' &&
           text[start + index] == needle[index]) {
      ++index;
    }
    if (needle[index] == '\0') {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool has_entry(Node directory, const char* name, Type type) {
  for (osplay::u32 index = 0;
       index < Filesystem::entry_count(directory); ++index) {
    const auto entry = Filesystem::entry(directory, index);
    const char* actual = entry.name;
    const char* expected = name;
    while (*actual == *expected && *actual != '\0') {
      ++actual;
      ++expected;
    }
    if (*actual == *expected && entry.type == type) {
      return true;
    }
  }
  return false;
}

}  // namespace

int main() {
  osplay::test::Suite suite{"kernel/pseudo_filesystem"};

  OSPLAY_CHECK(suite, Filesystem::lookup("/dev/null") == Node::dev_null);
  OSPLAY_CHECK(suite, !Filesystem::directory(Node::dev_null));
  OSPLAY_CHECK(suite, Filesystem::size(Node::dev_null) == 0);

  OSPLAY_CHECK(suite, Filesystem::mounted("/proc"));
  OSPLAY_CHECK(suite, Filesystem::mounted("/proc/net/tcp"));
  OSPLAY_CHECK(suite, Filesystem::mounted("/sys/kernel"));
  OSPLAY_CHECK(suite, !Filesystem::mounted("/procedure"));

  OSPLAY_CHECK(suite,
              Filesystem::lookup("/proc/net/tcp") == Node::proc_net_tcp);
  OSPLAY_CHECK(suite,
              Filesystem::lookup("/proc/net/dev") == Node::proc_net_dev);
  OSPLAY_CHECK(suite,
              Filesystem::lookup("/proc/net/tcp6") == Node::proc_net_tcp6);
  OSPLAY_CHECK(suite,
              Filesystem::lookup("/proc/net/udp") == Node::proc_net_udp);
  OSPLAY_CHECK(suite,
              Filesystem::lookup("/proc/net/udp6") == Node::proc_net_udp6);
  OSPLAY_CHECK(suite,
              Filesystem::lookup("/proc/net/raw") == Node::proc_net_raw);
  OSPLAY_CHECK(suite, Filesystem::lookup("/proc/net/missing") == Node::none);

  OSPLAY_CHECK(suite, Filesystem::directory(Node::proc));
  OSPLAY_CHECK(suite, Filesystem::directory(Node::proc_net));
  OSPLAY_CHECK(suite, !Filesystem::directory(Node::proc_net_tcp));
  OSPLAY_CHECK(suite,
              has_entry(Node::proc, "net", Type::directory));
  OSPLAY_CHECK(suite,
              has_entry(Node::proc_net, "tcp", Type::regular));
  OSPLAY_CHECK(suite,
              has_entry(Node::proc_net, "dev", Type::regular));
  OSPLAY_CHECK(suite,
              has_entry(Node::proc_net, "raw", Type::regular));
  OSPLAY_CHECK(suite,
              contains(Filesystem::contents(Node::proc_net_tcp),
                       "local_address"));
  OSPLAY_CHECK(suite, Filesystem::size(Node::proc_net_tcp) > 0);
  OSPLAY_CHECK(suite,
              contains(Filesystem::contents(Node::proc_net_dev), "eth0:"));

  OSPLAY_CHECK(suite, Filesystem::directory(Node::sys));
  OSPLAY_CHECK(suite,
              Filesystem::lookup("/sys/devices/system/cpu/online") ==
                  Node::sys_cpu_online);
  OSPLAY_CHECK(suite,
              has_entry(Node::sys_devices_system_cpu, "online",
                        Type::regular));
  OSPLAY_CHECK(suite,
              contains(Filesystem::contents(Node::sys_kernel_ostype),
                       "Linux"));

  return suite.finish();
}
