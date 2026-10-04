#include <mikos/net/interface.hpp>

constexpr mikos::network::InterfaceState state{};

#if defined(MIKOS_EXTREME) || defined(MIKOS_TRIBE) || defined(MIKOS_ROCKET)
static_assert(state.address == mikos::Ipv4Address{{192, 168, 76, 2}});
static_assert(state.broadcast == mikos::Ipv4Address{{192, 168, 76, 255}});
#else
static_assert(state.address == mikos::Ipv4Address{{10, 0, 2, 15}});
static_assert(state.broadcast == mikos::Ipv4Address{{10, 0, 2, 255}});
#endif
static_assert(state.netmask == mikos::Ipv4Address{{255, 255, 255, 0}});

int main() {}
