#include <drivers/storage/tribe_sd_dma.hpp>

#include <support/test.hpp>

namespace {

using osplay::u32;
using namespace osplay::drivers::tribe_sd::detail;

struct Write {
  u32 offset;
  u32 value;
};

struct Registers {
  Write writes[8]{};
  u32 statuses[8]{};
  u32 write_count{};
  u32 status_count{};
  u32 status_cursor{};
  u32 fence_count{};

  void write(u32 offset, u32 value) {
    writes[write_count++] = Write{offset, value};
  }

  [[nodiscard]] u32 read(u32 offset) {
    if (offset != status || status_cursor >= status_count) {
      return 0;
    }
    return statuses[status_cursor++];
  }

  void fence() { ++fence_count; }
};

void check_success(osplay::test::Suite& suite) {
  Registers registers;
  registers.statuses[0] = 0;
  registers.statuses[1] = status_done;
  registers.status_count = 2;

  OSPLAY_CHECK(suite, read_dma(registers, 7, 0x80001000, 512, 4));
  OSPLAY_CHECK(suite, registers.write_count == 6);
  OSPLAY_CHECK(suite, registers.writes[0].offset == control);
  OSPLAY_CHECK(suite, registers.writes[0].value == control_clear_done);
  OSPLAY_CHECK(suite, registers.writes[1].offset == command);
  OSPLAY_CHECK(suite, registers.writes[1].value == read_single_block);
  OSPLAY_CHECK(suite, registers.writes[2].offset == argument);
  OSPLAY_CHECK(suite, registers.writes[2].value == 7);
  OSPLAY_CHECK(suite, registers.writes[3].offset == length);
  OSPLAY_CHECK(suite, registers.writes[3].value == 512);
  OSPLAY_CHECK(suite, registers.writes[4].offset == dma_address);
  OSPLAY_CHECK(suite, registers.writes[4].value == 0x80001000);
  OSPLAY_CHECK(suite, registers.writes[5].offset == control);
  OSPLAY_CHECK(suite,
              registers.writes[5].value == (control_start | control_dma));
  OSPLAY_CHECK(suite, registers.status_cursor == 2);
  OSPLAY_CHECK(suite, registers.fence_count == 3);
}

void check_write_success(osplay::test::Suite& suite) {
  Registers registers;
  registers.statuses[0] = 0;
  registers.statuses[1] = status_done;
  registers.status_count = 2;

  OSPLAY_CHECK(suite, write_dma(registers, 11, 0x80003000, 1024, 4));
  OSPLAY_CHECK(suite, registers.write_count == 6);
  OSPLAY_CHECK(suite, registers.writes[0].offset == control);
  OSPLAY_CHECK(suite, registers.writes[0].value == control_clear_done);
  OSPLAY_CHECK(suite, registers.writes[1].offset == command);
  OSPLAY_CHECK(suite, registers.writes[1].value == write_single_block);
  OSPLAY_CHECK(suite, registers.writes[2].offset == argument);
  OSPLAY_CHECK(suite, registers.writes[2].value == 11);
  OSPLAY_CHECK(suite, registers.writes[3].offset == length);
  OSPLAY_CHECK(suite, registers.writes[3].value == 1024);
  OSPLAY_CHECK(suite, registers.writes[4].offset == dma_address);
  OSPLAY_CHECK(suite, registers.writes[4].value == 0x80003000);
  OSPLAY_CHECK(suite, registers.writes[5].offset == control);
  OSPLAY_CHECK(suite,
              registers.writes[5].value ==
                  (control_start | control_dma | control_write));
  OSPLAY_CHECK(suite, registers.status_cursor == 2);
  OSPLAY_CHECK(suite, registers.fence_count == 3);
}

void check_failure_paths(osplay::test::Suite& suite) {
  Registers error_registers;
  error_registers.statuses[0] = status_error | status_done;
  error_registers.status_count = 1;
  OSPLAY_CHECK(
      suite, !read_dma(error_registers, 0, 0x80002000, 512, 2));
  OSPLAY_CHECK(suite, error_registers.status_cursor == 1);
  OSPLAY_CHECK(suite, error_registers.fence_count == 2);

  Registers timeout_registers;
  timeout_registers.status_count = 2;
  OSPLAY_CHECK(
      suite, !read_dma(timeout_registers, 0, 0x80002000, 512, 2));
  OSPLAY_CHECK(suite, timeout_registers.status_cursor == 2);

  Registers invalid_registers;
  OSPLAY_CHECK(suite,
              !read_dma(invalid_registers, 0, 0x80002001, 512, 2));
  OSPLAY_CHECK(suite, !read_dma(invalid_registers, 0, 0, 512, 2));
  OSPLAY_CHECK(suite,
              !read_dma(invalid_registers, 0, 0x80002000, 0, 2));
  OSPLAY_CHECK(suite, invalid_registers.write_count == 0);
  OSPLAY_CHECK(suite, invalid_registers.fence_count == 0);
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/tribe_sd_dma"};
  check_success(suite);
  check_write_success(suite);
  check_failure_paths(suite);
  return suite.finish();
}
