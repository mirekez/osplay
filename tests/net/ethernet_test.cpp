#include <osplay/net/ethernet.hpp>

#include <support/test.hpp>

namespace {

constexpr osplay::MacAddress guest_mac{{0x52, 0x54, 0x00, 0x12, 0x34, 0x56}};
constexpr osplay::MacAddress peer_mac{{0x02, 0x00, 0x00, 0x00, 0x00, 0x02}};
constexpr osplay::Ipv4Address guest_ip{{10, 0, 2, 15}};
constexpr osplay::Ipv4Address peer_ip{{10, 0, 2, 2}};

struct [[gnu::packed]] EchoPacket {
  osplay::EthernetHeader ethernet;
  osplay::Ipv4Header ip;
  osplay::IcmpEchoHeader icmp;
  osplay::u8 payload[4];
};

[[nodiscard]] osplay::ArpIpv4 arp_request() {
  osplay::ArpIpv4 packet{};
  packet.ethernet.type = osplay::net16(0x0806);
  packet.hardware_type = osplay::net16(1);
  packet.protocol_type = osplay::net16(0x0800);
  packet.hardware_size = 6;
  packet.protocol_size = 4;
  packet.operation = osplay::net16(1);
  osplay::copy_octets(packet.sender_mac, peer_mac.octet);
  osplay::copy_octets(packet.sender_ip, peer_ip.octet);
  osplay::copy_octets(packet.target_ip, guest_ip.octet);
  return packet;
}

[[nodiscard]] EchoPacket echo_request() {
  EchoPacket packet{};
  osplay::copy_octets(packet.ethernet.source, peer_mac.octet);
  osplay::copy_octets(packet.ethernet.destination, guest_mac.octet);
  packet.ethernet.type = osplay::net16(0x0800);
  packet.ip.version_ihl = 0x45;
  packet.ip.total_length =
      osplay::net16(sizeof(packet) - sizeof(osplay::EthernetHeader));
  packet.ip.ttl = 32;
  packet.ip.protocol = 1;
  osplay::copy_octets(packet.ip.source, peer_ip.octet);
  osplay::copy_octets(packet.ip.destination, guest_ip.octet);
  packet.icmp.type = 8;
  packet.icmp.identifier = osplay::net16(7);
  packet.icmp.sequence = osplay::net16(9);
  packet.payload[0] = 0xde;
  packet.payload[1] = 0xad;
  packet.payload[2] = 0xbe;
  packet.payload[3] = 0xef;
  packet.icmp.checksum = osplay::net16(osplay::internet_checksum(
      reinterpret_cast<const osplay::u8*>(&packet.icmp),
      sizeof(packet) - sizeof(packet.ethernet) - sizeof(packet.ip)));
  packet.ip.checksum = osplay::net16(osplay::internet_checksum(
      reinterpret_cast<const osplay::u8*>(&packet.ip), sizeof(packet.ip)));
  return packet;
}

}  // namespace

int main() {
  osplay::test::Suite suite{"net/ethernet"};

  auto arp = arp_request();
  const auto arp_size = osplay::make_arp_reply(
      reinterpret_cast<osplay::u8*>(&arp), sizeof(arp), guest_mac, guest_ip);
  OSPLAY_CHECK(suite, arp_size == sizeof(arp));
  OSPLAY_CHECK(suite, arp.operation == osplay::net16(2));
  OSPLAY_CHECK(suite, arp.ethernet.destination[5] == 2);
  OSPLAY_CHECK(suite, arp.sender_mac[0] == 0x52);
  OSPLAY_CHECK(suite, arp.sender_mac[5] == 0x56);

  auto wrong_arp = arp_request();
  wrong_arp.target_ip[3] = 99;
  OSPLAY_CHECK(suite,
              osplay::make_arp_reply(reinterpret_cast<osplay::u8*>(&wrong_arp),
                                    sizeof(wrong_arp), guest_mac, guest_ip) ==
                  0);
  OSPLAY_CHECK(suite,
              osplay::make_arp_reply(reinterpret_cast<osplay::u8*>(&wrong_arp),
                                    sizeof(wrong_arp) - 1, guest_mac,
                                    guest_ip) == 0);

  auto echo = echo_request();
  const auto echo_size = osplay::make_icmp_echo_reply(
      reinterpret_cast<osplay::u8*>(&echo), sizeof(echo), guest_mac, guest_ip);
  OSPLAY_CHECK(suite, echo_size == sizeof(echo));
  OSPLAY_CHECK(suite, echo.icmp.type == 0);
  OSPLAY_CHECK(suite, echo.ip.ttl == 64);
  OSPLAY_CHECK(suite,
              osplay::internet_checksum(
                  reinterpret_cast<const osplay::u8*>(&echo.ip),
                  sizeof(echo.ip)) == 0);
  OSPLAY_CHECK(suite,
              osplay::internet_checksum(
                  reinterpret_cast<const osplay::u8*>(&echo.icmp),
                  sizeof(echo) - sizeof(echo.ethernet) - sizeof(echo.ip)) ==
                  0);

  auto corrupt_echo = echo_request();
  corrupt_echo.payload[0] ^= 1;
  OSPLAY_CHECK(
      suite,
      osplay::make_icmp_echo_reply(
          reinterpret_cast<osplay::u8*>(&corrupt_echo), sizeof(corrupt_echo),
          guest_mac, guest_ip) == 0);

  return suite.finish();
}
