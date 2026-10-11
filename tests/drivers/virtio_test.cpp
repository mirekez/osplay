#include <drivers/net/virtio.hpp>

#include <support/test.hpp>

int main() {
  osplay::test::Suite suite{"drivers/virtio"};
  using osplay::drivers::virtio::Feature;
  using osplay::drivers::virtio::FeatureSet;

  constexpr FeatureSet requested{Feature::mac, Feature::version_1};
  constexpr auto offered = FeatureSet::from_banks(0xffffffffu, 1);
  constexpr auto accepted = offered & requested;
  const auto absent = FeatureSet::from_banks(0, 0) & requested;

  OSPLAY_CHECK(suite, requested.low() == 1u << 5);
  OSPLAY_CHECK(suite, requested.high() == 1);
  OSPLAY_CHECK(suite, requested.contains(Feature::mac));
  OSPLAY_CHECK(suite, requested.contains(Feature::version_1));
  OSPLAY_CHECK(suite, accepted.low() == 1u << 5);
  OSPLAY_CHECK(suite, accepted.high() == 1);
  OSPLAY_CHECK(suite, !absent.contains(Feature::mac));

  osplay::drivers::virtio::SplitQueue<4> queue;
  queue.reset();
  osplay::u8 first[4]{};
  osplay::u8 second[8]{};
  queue.describe(0, first, sizeof(first),
                 osplay::drivers::virtio::Access::device_reads, 1);
  queue.describe(1, second, sizeof(second),
                 osplay::drivers::virtio::Access::device_writes);
  OSPLAY_CHECK(suite, queue.descriptor(0).flags == 1);
  OSPLAY_CHECK(suite, queue.descriptor(0).next == 1);
  OSPLAY_CHECK(suite, queue.descriptor(1).flags == 2);
  OSPLAY_CHECK(suite, queue.descriptor(1).next == 0);

  return suite.finish();
}
