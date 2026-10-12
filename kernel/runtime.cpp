#include <osplay/base.hpp>

extern "C" osplay::usize strlen(const char* text) {
  osplay::usize length = 0;
  while (text[length] != '\0') ++length;
  return length;
}
extern "C" int strcmp(const char* first, const char* second) {
  while (*first != '\0' && *first == *second) { ++first; ++second; }
  return static_cast<unsigned char>(*first) - static_cast<unsigned char>(*second);
}
extern "C" int strncmp(const char* first, const char* second, osplay::usize size) {
  while (size != 0) {
    const int result = static_cast<unsigned char>(*first) - static_cast<unsigned char>(*second);
    if (result != 0 || *first == '\0') return result;
    ++first; ++second; --size;
  }
  return 0;
}
extern "C" char* strcpy(char* output, const char* input) {
  char* begin = output;
  do { *output++ = *input; } while (*input++ != '\0');
  return begin;
}

extern "C" void* memcpy(void* destination, const void* source,
                        osplay::usize size) {
  auto* out = static_cast<osplay::u8*>(destination);
  const auto* in = static_cast<const osplay::u8*>(source);
  using Word [[gnu::may_alias]] = osplay::uword;
  if (((reinterpret_cast<osplay::usize>(out) |
        reinterpret_cast<osplay::usize>(in)) &
       (sizeof(Word) - 1)) == 0) {
    constexpr osplay::usize words_per_block = 8;
    constexpr osplay::usize block_size = words_per_block * sizeof(Word);
    while (size >= block_size) {
      auto* word_out = reinterpret_cast<Word*>(out);
      const auto* word_in = reinterpret_cast<const Word*>(in);
      word_out[0] = word_in[0];
      word_out[1] = word_in[1];
      word_out[2] = word_in[2];
      word_out[3] = word_in[3];
      word_out[4] = word_in[4];
      word_out[5] = word_in[5];
      word_out[6] = word_in[6];
      word_out[7] = word_in[7];
      out += block_size;
      in += block_size;
      size -= block_size;
    }
    while (size >= sizeof(Word)) {
      *reinterpret_cast<Word*>(out) = *reinterpret_cast<const Word*>(in);
      out += sizeof(Word);
      in += sizeof(Word);
      size -= sizeof(Word);
    }
  }
  while (size != 0) {
    *out++ = *in++;
    --size;
  }
  return destination;
}

extern "C" void* memmove(void* destination, const void* source,
                         osplay::usize size) {
  auto* out = static_cast<osplay::u8*>(destination);
  const auto* in = static_cast<const osplay::u8*>(source);
  if (out < in) {
    for (osplay::usize i = 0; i < size; ++i) {
      out[i] = in[i];
    }
  } else if (out > in) {
    for (osplay::usize i = size; i != 0; --i) {
      out[i - 1] = in[i - 1];
    }
  }
  return destination;
}

extern "C" void* memset(void* destination, int value, osplay::usize size) {
  auto* out = static_cast<osplay::u8*>(destination);
  const auto byte = static_cast<osplay::u8>(value);
  using Word [[gnu::may_alias]] = osplay::uword;
  while (size != 0 &&
         (reinterpret_cast<osplay::usize>(out) &
          (sizeof(Word) - 1)) != 0) {
    *out++ = byte;
    --size;
  }
  const Word word = static_cast<Word>(byte) * (~Word{0} / 255);
  while (size >= 8 * sizeof(word)) {
    auto* words = reinterpret_cast<Word*>(out);
    words[0] = word;
    words[1] = word;
    words[2] = word;
    words[3] = word;
    words[4] = word;
    words[5] = word;
    words[6] = word;
    words[7] = word;
    out += 8 * sizeof(word);
    size -= 8 * sizeof(word);
  }
  while (size >= sizeof(word)) {
    *reinterpret_cast<Word*>(out) = word;
    out += sizeof(word);
    size -= sizeof(word);
  }
  while (size != 0) {
    *out++ = byte;
    --size;
  }
  return destination;
}

namespace {

[[nodiscard]] osplay::u64 divide_u64(osplay::u64 dividend,
                                    osplay::u64 divisor,
                                    osplay::u64* remainder) {
  if (divisor == 0) {
    for (;;) {
      asm volatile("wfi");
    }
  }
  osplay::u64 quotient = 0;
  osplay::u64 current = 0;
  for (osplay::u32 bit = 0; bit < 64; ++bit) {
    current = (current << 1) | (dividend >> 63);
    dividend <<= 1;
    quotient <<= 1;
    if (current >= divisor) {
      current -= divisor;
      quotient |= 1;
    }
  }
  if (remainder != nullptr) {
    *remainder = current;
  }
  return quotient;
}

}  // namespace

extern "C" osplay::u64 __udivdi3(osplay::u64 dividend,
                                  osplay::u64 divisor) {
  return divide_u64(dividend, divisor, nullptr);
}

extern "C" osplay::u64 __umoddi3(osplay::u64 dividend,
                                  osplay::u64 divisor) {
  osplay::u64 remainder{};
  static_cast<void>(divide_u64(dividend, divisor, &remainder));
  return remainder;
}

extern "C" int memcmp(const void* left, const void* right, osplay::usize size) {
  const auto* a = static_cast<const osplay::u8*>(left);
  const auto* b = static_cast<const osplay::u8*>(right);
  for (osplay::usize i = 0; i < size; ++i) {
    if (a[i] != b[i]) {
      return static_cast<int>(a[i]) - static_cast<int>(b[i]);
    }
  }
  return 0;
}
