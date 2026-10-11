#include <osplay/net/socket.hpp>
#include <type_traits>

#include <support/test.hpp>

namespace {

using osplay::network::SocketHandle;
using osplay::network::SocketTable;

static_assert(std::is_trivially_copyable_v<SocketHandle>);
static_assert(!std::is_constructible_v<SocketHandle, osplay::u8>);
static_assert(!std::is_constructible_v<SocketHandle, int>);
static_assert(!std::is_convertible_v<SocketHandle, std::size_t>);
static_assert(!std::is_invocable_v<decltype(&SocketTable::retain),
                                 SocketTable&, int>);
static_assert(std::is_same_v<decltype(SocketHandle{}.index()), std::size_t>);
static_assert(SocketHandle{} == osplay::network::invalid_socket);

constexpr osplay::Ipv4Address any{{0, 0, 0, 0}};
constexpr osplay::Ipv4Address local_ip{{192, 168, 76, 2}};
constexpr osplay::Ipv4Address peer_ip{{192, 168, 76, 1}};
constexpr osplay::MacAddress peer_mac{{2, 0, 0, 0, 0, 1}};

}  // namespace

int main() {
  osplay::test::Suite suite{"net/socket"};
  using osplay::abi::socket::Type;
  using osplay::network::Endpoint;
  using osplay::network::SocketResult;
  using osplay::network::SocketState;
  osplay::network::SocketTable sockets;

  OSPLAY_CHECK(suite, sockets.slot(SocketHandle{}) == nullptr);
  OSPLAY_CHECK(suite, sockets.retain(SocketHandle{}) == SocketResult::bad_handle);
  OSPLAY_CHECK(suite, sockets.release(SocketHandle{}) == SocketResult::bad_handle);
  OSPLAY_CHECK(suite, sockets.listener({local_ip, 22}) ==
                         osplay::network::invalid_socket);

  // Fill every reassembly slot across SEQ wraparound, preserve duplicates
  // even when full, drain in order, then reuse slots after reset/close.
  {
    using namespace osplay;
    network::SocketTable table;
    const auto listen = table.open(Type::stream);
    OSPLAY_CHECK(suite, table.bind(listen.handle, {any, 22}) == SocketResult::success);
    OSPLAY_CHECK(suite, table.listen(listen.handle, 2) == SocketResult::success);
    const auto child = table.begin_connection(listen.handle, {local_ip, 22},
        {peer_ip, 49000}, peer_mac, 0xfffffff0u, 100);
    OSPLAY_CHECK(suite, table.establish(child.handle, 101) == SocketResult::success);
    const u32 start = table.slot(child.handle)->receive_next;
    for (u32 i = 1; i <= network::reassembly_capacity; ++i) {
      const u8 byte = static_cast<u8>(i);
      OSPLAY_CHECK(suite, table.receive(child.handle, start + i, &byte, 1, false).result == SocketResult::success);
    }
    const u8 different = 99;
    OSPLAY_CHECK(suite, table.receive(child.handle, start + 1, &different, 1, false).result == SocketResult::success);
    OSPLAY_CHECK(suite, table.receive(child.handle, start + 17, &different, 1, false).result == SocketResult::no_space);
    const u8 first = 0;
    OSPLAY_CHECK(suite, table.receive(child.handle, start, &first, 1, false).size == 17);
    u8 output[17]{};
    OSPLAY_CHECK(suite, table.read(child.handle, output, 17).size == 17);
    for (u32 i = 0; i < 17; ++i) OSPLAY_CHECK(suite, output[i] == i);
    for (u32 i = 1; i <= network::reassembly_capacity; ++i)
      OSPLAY_CHECK(suite, table.receive(child.handle, start + 17 + i, &different, 1, false).result == SocketResult::success);
    table.reset(child.handle);
    const auto second = table.begin_connection(listen.handle, {local_ip, 22},
        {peer_ip, 49001}, peer_mac, 500, 200);
    OSPLAY_CHECK(suite, table.establish(second.handle, 201) == SocketResult::success);
    for (u32 i = 1; i <= network::reassembly_capacity; ++i)
      OSPLAY_CHECK(suite, table.receive(second.handle, 501 + i, &different, 1, false).result == SocketResult::success);
    OSPLAY_CHECK(suite, table.release(second.handle) == SocketResult::success);
  }

  const auto control = sockets.open(Type::datagram);
  OSPLAY_CHECK(suite, control.result == SocketResult::success);
  OSPLAY_CHECK(suite, sockets.slot(control.handle)->state ==
                         SocketState::control);
  OSPLAY_CHECK(suite, sockets.bind(control.handle, {any, 22}) ==
                         SocketResult::wrong_type);

  const auto listener = sockets.open(Type::stream);
  OSPLAY_CHECK(suite, listener.result == SocketResult::success);
  OSPLAY_CHECK(suite, sockets.listen(listener.handle, 4) ==
                         SocketResult::not_bound);
  OSPLAY_CHECK(suite, sockets.bind(listener.handle, {any, 22}) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, sockets.listen(listener.handle, 99) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, sockets.slot(listener.handle)->backlog == 4);
  OSPLAY_CHECK(suite, sockets.listener({local_ip, 22}) == listener.handle);
  OSPLAY_CHECK(suite, !sockets.readable(listener.handle));

  const auto duplicate = sockets.open(Type::stream);
  OSPLAY_CHECK(suite, sockets.bind(duplicate.handle, {local_ip, 22}) ==
                         SocketResult::address_in_use);
  OSPLAY_CHECK(suite, sockets.release(duplicate.handle) ==
                         SocketResult::success);

  const auto pending = sockets.begin_connection(
      listener.handle, Endpoint{local_ip, 22}, Endpoint{peer_ip, 49152},
      peer_mac, 1000, 5000);
  OSPLAY_CHECK(suite, pending.result == SocketResult::success);
  OSPLAY_CHECK(suite, sockets.slot(pending.handle)->receive_next == 1001);
  OSPLAY_CHECK(suite, sockets.slot(pending.handle)->send_next == 5001);
  const auto retransmit = sockets.begin_connection(
      listener.handle, Endpoint{local_ip, 22}, Endpoint{peer_ip, 49152},
      peer_mac, 1000, 9000);
  OSPLAY_CHECK(suite, retransmit.handle == pending.handle);
  OSPLAY_CHECK(suite, sockets.establish(pending.handle, 5000) ==
                         SocketResult::invalid_argument);
  OSPLAY_CHECK(suite, sockets.establish(pending.handle, 5001) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, sockets.readable(listener.handle));

  const auto accepted = sockets.accept(listener.handle);
  OSPLAY_CHECK(suite, accepted.result == SocketResult::success);
  OSPLAY_CHECK(suite, accepted.handle == pending.handle);
  OSPLAY_CHECK(suite, (accepted.peer == Endpoint{peer_ip, 49152}));
  OSPLAY_CHECK(suite, sockets.accept(listener.handle).result ==
                         SocketResult::would_block);

  const osplay::u8 hello[]{'h', 'e', 'l', 'l', 'o'};
  OSPLAY_CHECK(suite,
              sockets.receive(accepted.handle, 996, hello, sizeof(hello), false)
                      .result == SocketResult::success);
  const auto received = sockets.receive(accepted.handle, 1001, hello,
                                        sizeof(hello), false);
  OSPLAY_CHECK(suite, received.result == SocketResult::success);
  OSPLAY_CHECK(suite, received.size == sizeof(hello));
  OSPLAY_CHECK(suite, sockets.readable(accepted.handle));
  osplay::u8 first[2]{};
  const auto first_read = sockets.read(accepted.handle, first, sizeof(first));
  OSPLAY_CHECK(suite, first_read.size == 2);
  OSPLAY_CHECK(suite, first[0] == 'h' && first[1] == 'e');
  osplay::u8 rest[8]{};
  const auto rest_read = sockets.read(accepted.handle, rest, sizeof(rest));
  OSPLAY_CHECK(suite, rest_read.size == 3);
  OSPLAY_CHECK(suite, rest[0] == 'l' && rest[2] == 'o');
  OSPLAY_CHECK(suite, sockets.read(accepted.handle, rest, sizeof(rest)).result ==
                         SocketResult::would_block);

  // A retransmission may start before receive_next yet contain an unseen tail.
  // Dropping it wholesale permanently wedges TCP after a partial-window read.
  const osplay::u8 overlapping[]{'l', 'l', 'o', '!'};
  const auto overlap = sockets.receive(accepted.handle, 1003, overlapping,
                                       sizeof(overlapping), false);
  OSPLAY_CHECK(suite, overlap.result == SocketResult::success);
  OSPLAY_CHECK(suite, overlap.size == 1);
  OSPLAY_CHECK(suite, sockets.read(accepted.handle, rest, sizeof(rest)).size ==
                         1);
  OSPLAY_CHECK(suite, rest[0] == '!');

  OSPLAY_CHECK(suite,
              sockets.receive(accepted.handle, 1007, nullptr, 0, true).result ==
                  SocketResult::success);
  OSPLAY_CHECK(suite, sockets.read(accepted.handle, rest, sizeof(rest)).result ==
                         SocketResult::end_of_file);
  OSPLAY_CHECK(suite, sockets.writable(accepted.handle));

  OSPLAY_CHECK(suite, sockets.retain(accepted.handle) == SocketResult::success);
  OSPLAY_CHECK(suite, sockets.release(accepted.handle) == SocketResult::success);
  OSPLAY_CHECK(suite, sockets.slot(accepted.handle) != nullptr);
  OSPLAY_CHECK(suite, sockets.release(accepted.handle) == SocketResult::success);
  OSPLAY_CHECK(suite, sockets.slot(accepted.handle) == nullptr);
  OSPLAY_CHECK(suite, sockets.release(listener.handle) == SocketResult::success);
  OSPLAY_CHECK(suite, sockets.release(control.handle) == SocketResult::success);

  osplay::network::SocketTable reordered;
  const auto reordered_listener = reordered.open(Type::stream);
  OSPLAY_CHECK(suite,
              reordered.bind(reordered_listener.handle, {any, 24}) ==
                  SocketResult::success);
  OSPLAY_CHECK(suite, reordered.listen(reordered_listener.handle, 1) ==
                         SocketResult::success);
  const auto reordered_child = reordered.begin_connection(
      reordered_listener.handle, {local_ip, 24}, {peer_ip, 42000}, peer_mac,
      100, 200);
  OSPLAY_CHECK(suite, reordered.establish(reordered_child.handle, 201) ==
                         SocketResult::success);
  const osplay::u8 tail[]{'l', 'l', 'o'};
  OSPLAY_CHECK(
      suite,
      reordered.receive(reordered_child.handle, 103, tail, sizeof(tail), true)
              .result == SocketResult::success);
  OSPLAY_CHECK(suite, !reordered.readable(reordered_child.handle));
  const osplay::u8 head[]{'h', 'e', 'l', 'l'};
  const auto assembled = reordered.receive(reordered_child.handle, 101, head,
                                           sizeof(head), false);
  OSPLAY_CHECK(suite, assembled.result == SocketResult::success);
  OSPLAY_CHECK(suite, assembled.size == 5);
  osplay::u8 assembled_output[8]{};
  OSPLAY_CHECK(suite,
              reordered
                      .read(reordered_child.handle, assembled_output,
                            sizeof(assembled_output))
                      .size == 5);
  OSPLAY_CHECK(suite, assembled_output[0] == 'h' &&
                         assembled_output[4] == 'o');
  OSPLAY_CHECK(suite,
              reordered
                      .read(reordered_child.handle, assembled_output,
                            sizeof(assembled_output))
                      .result == SocketResult::end_of_file);
  OSPLAY_CHECK(suite, reordered.writable(reordered_child.handle));

  osplay::network::SocketTable conflicts;
  const auto exact_one = conflicts.open(Type::stream);
  const auto exact_two = conflicts.open(Type::stream);
  OSPLAY_CHECK(suite, conflicts.bind(exact_one.handle, {local_ip, 80}) ==
                         SocketResult::success);
  OSPLAY_CHECK(
      suite,
      conflicts.bind(exact_two.handle,
                     {osplay::Ipv4Address{{192, 168, 76, 3}}, 80}) ==
          SocketResult::success);
  const auto wildcard = conflicts.open(Type::stream);
  OSPLAY_CHECK(suite, conflicts.bind(wildcard.handle, {any, 80}) ==
                         SocketResult::address_in_use);
  const auto ephemeral_one = conflicts.open(Type::stream);
  const auto ephemeral_two = conflicts.open(Type::stream);
  OSPLAY_CHECK(suite, conflicts.bind(ephemeral_one.handle, {any, 0}) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, conflicts.bind(ephemeral_two.handle, {any, 0}) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, conflicts.slot(ephemeral_one.handle)->local.port !=
                         conflicts.slot(ephemeral_two.handle)->local.port);

  osplay::network::SocketTable backlog;
  const auto backlog_listener = backlog.open(Type::stream);
  OSPLAY_CHECK(suite, backlog.bind(backlog_listener.handle, {any, 22}) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, backlog.listen(backlog_listener.handle, 1) ==
                         SocketResult::success);
  const auto first_pending = backlog.begin_connection(
      backlog_listener.handle, {local_ip, 22}, {peer_ip, 40000}, peer_mac, 1,
      10);
  OSPLAY_CHECK(suite, first_pending.result == SocketResult::success);
  OSPLAY_CHECK(suite,
              backlog
                      .begin_connection(backlog_listener.handle,
                                        {local_ip, 22}, {peer_ip, 40001},
                                        peer_mac, 1, 20)
                      .result == SocketResult::no_space);
  OSPLAY_CHECK(suite, backlog.release(backlog_listener.handle) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, backlog.slot(first_pending.handle) == nullptr);

  osplay::network::SocketTable reset_table;
  const auto reset_listener = reset_table.open(Type::stream);
  OSPLAY_CHECK(suite, reset_table.bind(reset_listener.handle, {any, 23}) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, reset_table.listen(reset_listener.handle, 1) ==
                         SocketResult::success);
  const auto reset_child = reset_table.begin_connection(
      reset_listener.handle, {local_ip, 23}, {peer_ip, 41000}, peer_mac, 1,
      20);
  OSPLAY_CHECK(suite, reset_table.establish(reset_child.handle, 21) ==
                         SocketResult::success);
  reset_table.reset(reset_child.handle);
  OSPLAY_CHECK(suite, reset_table.readable(reset_listener.handle));
  const auto reset_accepted = reset_table.accept(reset_listener.handle);
  OSPLAY_CHECK(suite, reset_accepted.result == SocketResult::success);
  osplay::u8 reset_output{};
  OSPLAY_CHECK(suite,
              reset_table.read(reset_accepted.handle, &reset_output, 1).result ==
                  SocketResult::reset);

  // Fill the receive window with all but the last byte of one TCP segment,
  // drain it, then retransmit the original segment. Only its unseen tail must
  // be appended; this is the overlap case that matters for SSH bursts.
  osplay::network::SocketTable saturated;
  const auto saturated_listener = saturated.open(Type::stream);
  OSPLAY_CHECK(suite, saturated.bind(saturated_listener.handle, {any, 25}) ==
                         SocketResult::success);
  OSPLAY_CHECK(suite, saturated.listen(saturated_listener.handle, 1) ==
                         SocketResult::success);
  const auto saturated_child = saturated.begin_connection(
      saturated_listener.handle, {local_ip, 25}, {peer_ip, 43000}, peer_mac,
      1000, 5000);
  OSPLAY_CHECK(suite, saturated.establish(saturated_child.handle, 5001) ==
                         SocketResult::success);
  osplay::u8 large_segment[osplay::network::socket_receive_capacity + 1]{};
  large_segment[osplay::network::socket_receive_capacity] = 0x5a;
  const auto partial = saturated.receive(
      saturated_child.handle, 1001, large_segment, sizeof(large_segment),
      false);
  OSPLAY_CHECK(suite, partial.size ==
                         osplay::network::socket_receive_capacity);
  osplay::u8 drained[osplay::network::socket_receive_capacity]{};
  OSPLAY_CHECK(suite,
              saturated
                      .read(saturated_child.handle, drained, sizeof(drained))
                      .size == osplay::network::socket_receive_capacity);
  const auto tail_after_retry = saturated.receive(
      saturated_child.handle, 1001, large_segment, sizeof(large_segment),
      false);
  OSPLAY_CHECK(suite, tail_after_retry.result == SocketResult::success);
  OSPLAY_CHECK(suite, tail_after_retry.size == 1);
  OSPLAY_CHECK(suite,
              saturated.read(saturated_child.handle, &reset_output, 1).size ==
                  1);
  OSPLAY_CHECK(suite, reset_output == 0x5a);

  osplay::network::SocketTable capacity;
  std::array<SocketHandle, osplay::network::socket_capacity> handles;
  for (auto& handle : handles) {
    const auto opened = capacity.open(Type::stream);
    OSPLAY_CHECK(suite, opened.result == SocketResult::success);
    handle = opened.handle;
    OSPLAY_CHECK(suite, handle != osplay::network::invalid_socket);
    OSPLAY_CHECK(suite, capacity.slot(handle) != nullptr);
  }
  for (const auto handle : handles) {
    OSPLAY_CHECK(suite, std::ranges::count(handles, handle) == 1);
  }
  const auto exhausted = capacity.open(Type::stream);
  OSPLAY_CHECK(suite, exhausted.result == SocketResult::no_space);
  OSPLAY_CHECK(suite, exhausted.handle == osplay::network::invalid_socket);
  OSPLAY_CHECK(suite, capacity.release(handles.back()) == SocketResult::success);
  OSPLAY_CHECK(suite, capacity.slot(handles.back()) == nullptr);
  const auto reused = capacity.open(Type::stream);
  OSPLAY_CHECK(suite, reused.result == SocketResult::success);
  OSPLAY_CHECK(suite, reused.handle == handles.back());

  return suite.finish();
}
