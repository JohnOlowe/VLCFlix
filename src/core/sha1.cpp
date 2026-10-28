#include "core/sha1.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace vf {
namespace {

inline std::uint32_t rol(std::uint32_t value, int bits) {
  return (value << bits) | (value >> (32 - bits));
}

}  // namespace

std::string sha1_hex(const std::string& input) {
  std::uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

  std::vector<std::uint8_t> message(input.begin(), input.end());
  const std::uint64_t bit_length = static_cast<std::uint64_t>(message.size()) * 8;
  message.push_back(0x80);
  while (message.size() % 64 != 56) message.push_back(0);
  for (int i = 7; i >= 0; --i) {
    message.push_back(static_cast<std::uint8_t>((bit_length >> (i * 8)) & 0xFF));
  }

  for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<std::uint32_t>(message[chunk + i * 4]) << 24) |
             (static_cast<std::uint32_t>(message[chunk + i * 4 + 1]) << 16) |
             (static_cast<std::uint32_t>(message[chunk + i * 4 + 2]) << 8) |
             (static_cast<std::uint32_t>(message[chunk + i * 4 + 3]));
    }
    for (int i = 16; i < 80; ++i) {
      w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      std::uint32_t f = 0;
      std::uint32_t k = 0;
      if (i < 20) {
        f = (b & c) | ((~b) & d);
        k = 0x5A827999u;
      } else if (i < 40) {
        f = b ^ c ^ d;
        k = 0x6ED9EBA1u;
      } else if (i < 60) {
        f = (b & c) | (b & d) | (c & d);
        k = 0x8F1BBCDCu;
      } else {
        f = b ^ c ^ d;
        k = 0xCA62C1D6u;
      }
      const std::uint32_t temp = rol(a, 5) + f + e + k + w[i];
      e = d;
      d = c;
      c = rol(b, 30);
      b = a;
      a = temp;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
  }

  char out[41];
  std::snprintf(out, sizeof out, "%08x%08x%08x%08x%08x", h[0], h[1], h[2], h[3], h[4]);
  return out;
}

std::string item_id_for_path(const std::string& absolute_path) {
  return sha1_hex(absolute_path).substr(0, 12);
}

}  // namespace vf
