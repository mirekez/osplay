#include <osplay/process/snapshot_arena.hpp>

#include <support/test.hpp>

int main() {
  using namespace osplay::process_model;
  osplay::test::Suite suite{"kernel/snapshot_arena"};

  SnapshotArena<8> arena;
  OSPLAY_CHECK(suite, arena.initialize(0x1003, 0x2007) ==
                         SnapshotArenaStatus::success);
  OSPLAY_CHECK(suite, arena.capacity() == 0xff0);
  OSPLAY_CHECK(suite, arena.available() == arena.capacity());
  OSPLAY_CHECK(suite, arena.invariant());

  const auto first = arena.allocate(1);
  const auto second = arena.allocate(0x101);
  const auto third = arena.allocate(0x200);
  OSPLAY_CHECK(suite, (first == SnapshotAllocation{0x1010, 0x10}));
  OSPLAY_CHECK(suite, (second == SnapshotAllocation{0x1020, 0x110}));
  OSPLAY_CHECK(suite, (third == SnapshotAllocation{0x1130, 0x200}));
  OSPLAY_CHECK(suite, arena.allocated() == 0x320);
  OSPLAY_CHECK(suite, arena.invariant());

  // Out-of-order releases produce separate extents, then coalesce on both
  // sides when the allocation between them is returned.
  OSPLAY_CHECK(suite, arena.release(first) == SnapshotArenaStatus::success);
  OSPLAY_CHECK(suite, arena.release(third) == SnapshotArenaStatus::success);
  OSPLAY_CHECK(suite, arena.free_extents() == 2);
  OSPLAY_CHECK(suite, arena.release(second) == SnapshotArenaStatus::success);
  OSPLAY_CHECK(suite, arena.free_extents() == 1);
  OSPLAY_CHECK(suite, arena.available() == arena.capacity());
  OSPLAY_CHECK(suite, arena.largest_available() == arena.capacity());
  OSPLAY_CHECK(suite, arena.invariant());

  const auto entire = arena.allocate(arena.capacity());
  OSPLAY_CHECK(suite, entire.valid());
  OSPLAY_CHECK(suite, !arena.allocate(16).valid());
  OSPLAY_CHECK(suite, arena.available() == 0);
  OSPLAY_CHECK(suite, arena.release(entire) == SnapshotArenaStatus::success);
  OSPLAY_CHECK(suite,
              arena.release(entire) == SnapshotArenaStatus::invalid_argument);
  OSPLAY_CHECK(suite,
              arena.release({0x1000, 0x10}) ==
                  SnapshotArenaStatus::invalid_argument);
  OSPLAY_CHECK(suite, arena.invariant());

  // Fork snapshots are limited only by actual arena availability. In
  // particular, retain a regression above the removed 512 KiB per-level
  // buffer limit.
  SnapshotArena<4> large_arena;
  OSPLAY_CHECK(suite, large_arena.initialize(0x100000, 0x500000) ==
                         SnapshotArenaStatus::success);
  const auto large = large_arena.allocate(0x180123);
  OSPLAY_CHECK(suite,
              (large == SnapshotAllocation{0x100000, 0x180130}));
  OSPLAY_CHECK(suite, large.size > 512 * 1024);
  OSPLAY_CHECK(suite, large_arena.invariant());
  OSPLAY_CHECK(suite,
              large_arena.release(large) == SnapshotArenaStatus::success);
  OSPLAY_CHECK(suite, large_arena.available() == large_arena.capacity());

  // Cover every release ordering for four simultaneously live snapshots.
  // Each prefix checks allocator accounting; every permutation must collapse
  // back to one extent spanning the complete arena.
  constexpr osplay::u8 release_orders[24][4] = {
      {0, 1, 2, 3}, {0, 1, 3, 2}, {0, 2, 1, 3}, {0, 2, 3, 1},
      {0, 3, 1, 2}, {0, 3, 2, 1}, {1, 0, 2, 3}, {1, 0, 3, 2},
      {1, 2, 0, 3}, {1, 2, 3, 0}, {1, 3, 0, 2}, {1, 3, 2, 0},
      {2, 0, 1, 3}, {2, 0, 3, 1}, {2, 1, 0, 3}, {2, 1, 3, 0},
      {2, 3, 0, 1}, {2, 3, 1, 0}, {3, 0, 1, 2}, {3, 0, 2, 1},
      {3, 1, 0, 2}, {3, 1, 2, 0}, {3, 2, 0, 1}, {3, 2, 1, 0},
  };
  for (const auto& order : release_orders) {
    SnapshotArena<8> permutation_arena;
    OSPLAY_CHECK(suite,
                permutation_arena.initialize(0x10000, 0x20000) ==
                    SnapshotArenaStatus::success);
    SnapshotAllocation allocations[4]{};
    allocations[0] = permutation_arena.allocate(0x111);
    allocations[1] = permutation_arena.allocate(0x222);
    allocations[2] = permutation_arena.allocate(0x333);
    allocations[3] = permutation_arena.allocate(0x444);
    for (const auto index : order) {
      OSPLAY_CHECK(suite, permutation_arena.release(allocations[index]) ==
                             SnapshotArenaStatus::success);
      OSPLAY_CHECK(suite, permutation_arena.invariant());
    }
    OSPLAY_CHECK(suite, permutation_arena.free_extents() == 1);
    OSPLAY_CHECK(suite, permutation_arena.available() ==
                           permutation_arena.capacity());
  }

  SnapshotArena<1> invalid;
  OSPLAY_CHECK(suite, invalid.initialize(0, 0x1000) ==
                         SnapshotArenaStatus::invalid_argument);
  OSPLAY_CHECK(suite, invalid.initialize(0x2000, 0x2000) ==
                         SnapshotArenaStatus::invalid_argument);

  return suite.finish();
}
