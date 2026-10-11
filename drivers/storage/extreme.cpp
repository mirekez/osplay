#include <drivers/storage/root_device.hpp>
#include <kernel/arch/extreme/userspace.hpp>
extern "C" void *memcpy(void *, const void *, osplay::usize);
namespace osplay::drivers::storage::root_device {
bool Device::initialize() { return true; }
u64 Device::sector_count() const { return extreme_platform::root_disk_bytes / sector_size; }
bool Device::read_sector(u64 sector, u8 *output) {
  return read_sectors(sector, output, sector_size);
}
bool Device::write_sector(u64 sector, const u8 *input) {
  return write_sectors(sector, input, sector_size);
}
bool Device::read_sectors(u64 sector, u8 *output, u32 count) {
  if (sector >= sector_count() ||
      count > extreme_platform::root_disk_bytes - u32(sector) * sector_size) {
    return false;
  }
  memcpy(output, extreme_platform::root_disk + u32(sector) * sector_size, count);
  return true;
}
bool Device::write_sectors(u64 sector, const u8 *input, u32 count) {
  if (sector >= sector_count() ||
      count > extreme_platform::root_disk_bytes - u32(sector) * sector_size) {
    return false;
  }
  memcpy(extreme_platform::root_disk + u32(sector) * sector_size, input, count);
  return true;
}
bool Device::flush() { return true; }
} // namespace osplay::drivers::storage::root_device
