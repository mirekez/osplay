// Rootless counterpart to ethgig_tap: libslirp supplies TCP/IP, while raw
// Ethernet still crosses the same UNIX datagram link to the simulated NIC.
#include <slirp/libslirp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <poll.h>
#include <string>
#include <vector>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {
volatile sig_atomic_t stopped = 0;
void stop(int) { stopped = 1; }
struct Timer { SlirpTimerCb callback; void* opaque; int64_t deadline = -1; };
struct Proxy {
  int fd = -1;
  sockaddr_un peer{};
  socklen_t peer_length = 0;
  std::vector<pollfd> polling;
  std::vector<Timer*> timers;
  std::deque<std::vector<uint8_t>> packets;
  uint64_t guest_frames = 0, host_frames = 0, dropped = 0;
  int64_t clock_scale = 1000;
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  static int64_t clock(void* opaque) {
    auto& p = *static_cast<Proxy*>(opaque);
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-p.start).count()/p.clock_scale;
  }
  static slirp_ssize_t send_packet(const void* data, size_t size, void* opaque) {
    auto& p = *static_cast<Proxy*>(opaque);
    if (size < 14 || size > 1514 || !p.peer_length) return size;
    if (p.packets.size() >= 256) { ++p.dropped; return size; }
    std::vector<uint8_t> packet{2, uint8_t(size>>8), uint8_t(size)};
    const auto* bytes = static_cast<const uint8_t*>(data);
    packet.insert(packet.end(), bytes, bytes+size);
    p.packets.push_back(std::move(packet));
    return size;
  }
  static void error(const char* message, void*) { fprintf(stderr, "slirp: %s\n", message); }
  static void* timer_new(SlirpTimerCb cb, void* cb_opaque, void* opaque) {
    auto& p = *static_cast<Proxy*>(opaque);
    auto* timer = new Timer{cb, cb_opaque}; p.timers.push_back(timer); return timer;
  }
  static void timer_free(void* timer, void* opaque) {
    auto& p = *static_cast<Proxy*>(opaque);
    auto* t = static_cast<Timer*>(timer);
    p.timers.erase(std::find(p.timers.begin(), p.timers.end(), t)); delete t;
  }
  static void timer_mod(void* timer, int64_t deadline, void*) { static_cast<Timer*>(timer)->deadline = deadline; }
  static void register_fd(int, void*) {}
  static void notify(void*) {}
  static int add_poll(int fd, int events, void* opaque) {
    auto& p = *static_cast<Proxy*>(opaque);
    short flags = 0;
    if (events & SLIRP_POLL_IN) flags |= POLLIN;
    if (events & SLIRP_POLL_OUT) flags |= POLLOUT;
    if (events & SLIRP_POLL_PRI) flags |= POLLPRI;
    p.polling.push_back({fd, flags, 0}); return p.polling.size()-1;
  }
  static int get_events(int i, void* opaque) {
    const auto flags = static_cast<Proxy*>(opaque)->polling.at(i).revents;
    int result = 0;
    if (flags & POLLIN) result |= SLIRP_POLL_IN;
    if (flags & POLLOUT) result |= SLIRP_POLL_OUT;
    if (flags & POLLPRI) result |= SLIRP_POLL_PRI;
    if (flags & POLLERR) result |= SLIRP_POLL_ERR;
    if (flags & POLLHUP) result |= SLIRP_POLL_HUP;
    return result;
  }
};
}

int main(int argc, char** argv) {
  std::string path = "/tmp/osplay-proxy.sock";
  int port = 2222;
  Proxy p;
  for (int i=1; i<argc; ++i) {
    if (!strcmp(argv[i], "--socket") && i+1<argc) path = argv[++i];
    else if (!strcmp(argv[i], "--ssh-port") && i+1<argc) port = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--clock-scale") && i+1<argc) p.clock_scale = atoll(argv[++i]);
    else { fprintf(stderr, "usage: %s [--socket PATH] [--ssh-port PORT] [--clock-scale N]\n", argv[0]); return 2; }
  }
  if (port < 1 || port > 65535 || p.clock_scale < 1) return 2;
  sockaddr_un address{};
  if (path.size() >= sizeof(address.sun_path)) return 2;
  address.sun_family = AF_UNIX; strcpy(address.sun_path, path.c_str());
  p.fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
  if (p.fd < 0 || bind(p.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) {
    perror("proxy socket (refusing to replace existing endpoint)"); return 1;
  }
  SlirpConfig config{};
  config.version = 4; config.restricted = 1; config.in_enabled = true;
  inet_pton(AF_INET, "192.168.76.0", &config.vnetwork);
  inet_pton(AF_INET, "255.255.255.0", &config.vnetmask);
  inet_pton(AF_INET, "192.168.76.1", &config.vhost);
  inet_pton(AF_INET, "192.168.76.2", &config.vdhcp_start);
  config.disable_dns = config.disable_dhcp = config.disable_host_loopback = true;
  config.if_mtu = config.if_mru = 1500;
  SlirpCb callbacks{};
  callbacks.send_packet = Proxy::send_packet; callbacks.guest_error = Proxy::error;
  callbacks.clock_get_ns = Proxy::clock; callbacks.timer_new = Proxy::timer_new;
  callbacks.timer_free = Proxy::timer_free; callbacks.timer_mod = Proxy::timer_mod;
  callbacks.register_poll_fd = callbacks.unregister_poll_fd = Proxy::register_fd;
  callbacks.notify = Proxy::notify;
  Slirp* slirp = slirp_new(&config, &callbacks, &p);
  in_addr host{}; inet_pton(AF_INET, "127.0.0.1", &host);
  if (!slirp || slirp_add_hostfwd(slirp, 0, host, port, config.vdhcp_start, 22)) {
    fprintf(stderr, "cannot create localhost SSH forward on %d\n", port);
    if (slirp) slirp_cleanup(slirp);
    close(p.fd); unlink(path.c_str()); return 1;
  }
  signal(SIGINT, stop); signal(SIGTERM, stop);
  printf("OSplay rootless proxy: 127.0.0.1:%d -> 192.168.76.2:22; socket=%s clock_scale=%ld libslirp=%s\n",
    port, path.c_str(), long(p.clock_scale), slirp_version_string()); fflush(stdout);
  while (!stopped) {
    p.polling = {{p.fd, POLLIN, 0}};
    uint32_t timeout = 100;
    slirp_pollfds_fill(slirp, &timeout, Proxy::add_poll, &p);
    const int wait = std::min<int64_t>(100, int64_t(timeout)*p.clock_scale);
    const int result = poll(p.polling.data(), p.polling.size(), wait);
    if (result < 0 && errno != EINTR) { perror("proxy poll"); break; }
    if (p.polling[0].revents & POLLIN) {
      for (;;) {
        std::array<uint8_t,2048> packet{};
        sockaddr_un remote{}; socklen_t length = sizeof(remote);
        const auto n = recvfrom(p.fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&remote), &length);
        if (n < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) perror("proxy recv"); break; }
        if (n == 1 && packet[0] == 1) { p.peer = remote; p.peer_length = length; p.packets.clear(); continue; }
        if (n < 3 || packet[0] != 2 || !p.peer_length ||
            length != p.peer_length || memcmp(&remote, &p.peer, length)) continue;
        const unsigned size = (unsigned(packet[1])<<8) | packet[2];
        if (size < 14 || size > 1514 || n != size+3) continue;
        ++p.guest_frames;
        slirp_input(slirp, packet.data()+3, size);
      }
    }
    slirp_pollfds_poll(slirp, result < 0, Proxy::get_events, &p);
    const int64_t now = Proxy::clock(&p)/1000000;
    const auto timers = p.timers;
    for (auto* timer: timers) if (timer->deadline >= 0 && timer->deadline <= now) {
      timer->deadline = -1; timer->callback(timer->opaque);
    }
    while (!p.packets.empty()) {
      const auto& packet = p.packets.front();
      const auto n = sendto(p.fd, packet.data(), packet.size(), 0, reinterpret_cast<sockaddr*>(&p.peer), p.peer_length);
      if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      if (n == ssize_t(packet.size())) ++p.host_frames; else ++p.dropped;
      p.packets.pop_front();
    }
  }
  printf("proxy frames guest=%lu host=%lu dropped=%lu\n", p.guest_frames, p.host_frames, p.dropped);
  slirp_cleanup(slirp); close(p.fd); unlink(path.c_str());
}
