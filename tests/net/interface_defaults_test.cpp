#include <osplay/net/interface.hpp>

constexpr osplay::network::InterfaceState state{};

#if defined(OSPLAY_EXTREME) || defined(OSPLAY_TRIBE) || defined(OSPLAY_ROCKET)
static_assert(state.address == osplay::Ipv4Address{{192, 168, 76, 2}});
static_assert(state.broadcast == osplay::Ipv4Address{{192, 168, 76, 255}});
#else
static_assert(state.address == osplay::Ipv4Address{{10, 0, 2, 15}});
static_assert(state.broadcast == osplay::Ipv4Address{{10, 0, 2, 255}});
#endif
static_assert(state.netmask == osplay::Ipv4Address{{255, 255, 255, 0}});

int main() {}
