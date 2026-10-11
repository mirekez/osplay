#include <drivers/storage/root_device.hpp>
extern "C" unsigned char __rocket_disk_begin[], __rocket_disk_end[];
extern "C" void* memcpy(void*, const void*, osplay::usize);
namespace osplay::drivers::storage::root_device {
bool Device::initialize() { return sector_count() != 0; }
u64 Device::sector_count() const { return (__rocket_disk_end - __rocket_disk_begin) / 512; }
bool Device::read_sector(u64 s, u8* p) { return read_sectors(s, p, 512); }
bool Device::write_sector(u64 s, const u8* p) { return write_sectors(s, p, 512); }
bool Device::read_sectors(u64 s, u8* p, u32 n) {
  if (s >= sector_count() || n > (sector_count() - s) * 512) return false;
  memcpy(p, __rocket_disk_begin + u32(s) * 512, n);
  return true;
}
bool Device::write_sectors(u64 s, const u8* p, u32 n) {
  if (s >= sector_count() || n > (sector_count() - s) * 512) return false;
  memcpy(__rocket_disk_begin + u32(s) * 512, p, n);
  return true;
}
bool Device::flush() { return true; }
}
