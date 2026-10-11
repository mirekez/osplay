#include <osplay/net/interface.hpp>

#include <support/test.hpp>

namespace {

[[nodiscard]] osplay::network::Ifreq32 request_for_eth0() {
  osplay::network::Ifreq32 request{};
  osplay::network::write_interface_name(request.name);
  return request;
}

[[nodiscard]] bool address_is(const osplay::network::Sockaddr32& address,
                              osplay::u8 a, osplay::u8 b, osplay::u8 c,
                              osplay::u8 d) {
  return address.family == 2 && address.data[2] == a &&
         address.data[3] == b && address.data[4] == c &&
         address.data[5] == d;
}

}  // namespace

int main() {
  osplay::test::Suite suite{"kernel/ioctl_syscall"};
  using osplay::network::InterfaceControlResult;

  OSPLAY_CHECK(suite, sizeof(osplay::network::Ifconf32) == 8);
  OSPLAY_CHECK(suite, osplay::network::siocgifconf == 0x8912);

  osplay::network::InterfaceState state{};
  state.mac = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x02}};

  auto request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifindex, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, request.value.index == 1);

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifmetric, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, request.value.metric == 0);

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifmtu, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, request.value.mtu == 1500);

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgiftxqlen, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, request.value.queue_length == 1000);

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifflags, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, (request.value.flags & osplay::network::interface_up) != 0);

  request = request_for_eth0();
  request.value.flags = static_cast<osplay::u16>(
      osplay::network::interface_broadcast | osplay::network::interface_running);
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocsifflags, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, state.flags == static_cast<osplay::u16>(request.value.flags));

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifaddr, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, address_is(request.value.address, 10, 0, 2, 15));

  request = request_for_eth0();
  osplay::network::write_sockaddr_ipv4(
      request.value.address, osplay::Ipv4Address{{192, 168, 76, 2}});
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocsifaddr, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite,
              (state.address == osplay::Ipv4Address{{192, 168, 76, 2}}));
  OSPLAY_CHECK(suite,
              (state.broadcast == osplay::Ipv4Address{{192, 168, 76, 255}}));

  request = request_for_eth0();
  osplay::network::write_sockaddr_ipv4(
      request.value.address, osplay::Ipv4Address{{255, 255, 255, 128}});
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocsifnetmask, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite,
              (state.netmask == osplay::Ipv4Address{{255, 255, 255, 128}}));
  OSPLAY_CHECK(suite,
              (state.broadcast == osplay::Ipv4Address{{192, 168, 76, 127}}));

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifnetmask, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, address_is(request.value.address, 255, 255, 255, 128));

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifbrdaddr, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, address_is(request.value.address, 192, 168, 76, 127));

  request = request_for_eth0();
  osplay::network::write_sockaddr_ipv4(
      request.value.address, osplay::Ipv4Address{{192, 168, 76, 126}});
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocsifbrdaddr, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite,
              (state.broadcast == osplay::Ipv4Address{{192, 168, 76, 126}}));

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifhwaddr, request) ==
                         InterfaceControlResult::success);
  OSPLAY_CHECK(suite, request.value.address.family == 1);
  OSPLAY_CHECK(suite, request.value.address.data[0] == 0x02);
  OSPLAY_CHECK(suite, request.value.address.data[5] == 0x02);

  request = request_for_eth0();
  request.value.address.family = 10;
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocsifaddr, request) ==
                         InterfaceControlResult::invalid_argument);

  request = request_for_eth0();
  request.name[3] = '1';
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, osplay::network::siocgifindex, request) ==
                         InterfaceControlResult::no_device);

  request = request_for_eth0();
  OSPLAY_CHECK(suite, osplay::network::apply_interface_ioctl(
                         state, 0xffffffff, request) ==
                         InterfaceControlResult::unsupported);

  return suite.finish();
}
