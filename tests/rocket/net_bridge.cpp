#include <svdpi.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
struct Bridge {
  int fd = -1;
  std::string path;
  std::vector<unsigned char> tx, rx;
  size_t cursor = 0;
  unsigned poll_count = 0;
  bool initialized = false;
  ~Bridge() { if (fd >= 0) close(fd); if (!path.empty()) unlink(path.c_str()); }
  [[noreturn]] void fail(const char* message) {
    std::fprintf(stderr, "Rocket network: %s: %s\n", message, std::strerror(errno));
    std::exit(1);
  }
  void init() {
    initialized = true;
    const char* peer = std::getenv("ROCKET_ETH_SOCKET");
    if (!peer || !*peer) return;
    fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (fd < 0) fail("socket");
    sockaddr_un local{}, remote{};
    local.sun_family = remote.sun_family = AF_UNIX;
    path = "/tmp/mikos-rocket-net-" + std::to_string(getpid()) + ".sock";
    if (path.size() >= sizeof(local.sun_path) || strlen(peer) >= sizeof(remote.sun_path)) {
      errno = ENAMETOOLONG; fail("socket path");
    }
    strcpy(local.sun_path, path.c_str()); strcpy(remote.sun_path, peer);
    if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local))) fail("bind");
    if (connect(fd, reinterpret_cast<sockaddr*>(&remote), sizeof(remote))) fail("connect");
    const unsigned char hello = 1;
    if (send(fd, &hello, 1, 0) != 1) fail("hello");
  }
  void transmit(unsigned long long data, unsigned char keep, bool last) {
    if (!keep || (keep & (unsigned(keep) + 1)) || (!last && keep != 255)) {
      errno = EPROTO; fail("invalid TX keep");
    }
    for (unsigned i = 0; i < 8; ++i) if (keep & (1u << i)) tx.push_back(data >> (8*i));
    if (tx.size() > 1514) { errno = EMSGSIZE; fail("oversize TX"); }
    if (last) {
      if (fd >= 0) {
        std::vector<unsigned char> packet{2, static_cast<unsigned char>(tx.size() >> 8),
                                        static_cast<unsigned char>(tx.size())};
        packet.insert(packet.end(), tx.begin(), tx.end());
        if (send(fd, packet.data(), packet.size(), 0) != ssize_t(packet.size())) fail("TX send");
      }
      tx.clear();
    }
  }
  void receive() {
    if (fd < 0 || !rx.empty() || (++poll_count & 255)) return;
    std::array<unsigned char, 2048> packet{};
    const auto n = recv(fd, packet.data(), packet.size(), 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNREFUSED) return;
      fail("RX recv");
    }
    if (n < 3 || packet[0] != 2) return;
    const unsigned length = (unsigned(packet[1]) << 8) | packet[2];
    if (length < 14 || length > 1514 || n != length + 3) return;
    rx.assign(packet.begin() + 3, packet.begin() + 3 + length);
    rx.resize(std::max(size_t(60), rx.size()), 0);
    cursor = 0;
  }
} bridge;
}
extern "C" void mikos_net_tick(svBit reset, svBit tx_fire,
  unsigned long long tx_data, unsigned char tx_keep, svBit tx_last,
  svBit rx_fire, svBit* valid, unsigned long long* data,
  unsigned char* keep, svBit* last) {
  *valid = *last = *keep = 0; *data = 0;
  if (reset) { bridge.tx.clear(); bridge.rx.clear(); bridge.cursor = 0; return; }
  if (!bridge.initialized) bridge.init();
  if (tx_fire) bridge.transmit(tx_data, tx_keep, tx_last);
  if (rx_fire) {
    bridge.cursor += std::min(size_t(8), bridge.rx.size() - bridge.cursor);
    if (bridge.cursor == bridge.rx.size()) { bridge.rx.clear(); bridge.cursor = 0; }
  }
  bridge.receive();
  if (!bridge.rx.empty()) {
    const auto n = std::min(size_t(8), bridge.rx.size() - bridge.cursor);
    *valid = 1; *last = bridge.cursor + n == bridge.rx.size();
    *keep = (1u << n) - 1;
    for (size_t i = 0; i < n; ++i) *data |= (unsigned long long)bridge.rx[bridge.cursor + i] << (8*i);
  }
}
