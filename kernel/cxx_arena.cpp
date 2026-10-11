#include <osplay/memory/arena.hpp>
#include <new>

namespace osplay::memory {
Arena::Block* Arena::make_block(void* address, usize bytes, Block* next) {
  return ::new (address) Block{bytes, next, nullptr};
}
}  // namespace osplay::memory
