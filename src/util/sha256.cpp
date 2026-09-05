#include "tradutorlinux/util/sha256.hpp"

#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace tradutorlinux::util {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
    0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
    0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
    0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
};

constexpr std::array<std::uint32_t, 8> kInitialState{
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
};

[[nodiscard]] constexpr std::uint32_t rotate_right(const std::uint32_t value,
                                                    const std::uint32_t amount) noexcept {
    return (value >> amount) | (value << (32U - amount));
}

[[nodiscard]] constexpr std::uint32_t choose(const std::uint32_t x,
                                             const std::uint32_t y,
                                             const std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}

[[nodiscard]] constexpr std::uint32_t majority(const std::uint32_t x,
                                               const std::uint32_t y,
                                               const std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}

class Sha256 {
public:
    Sha256() noexcept : state_(kInitialState) {}

    void update(const std::uint8_t* data, const std::size_t size) noexcept {
        bit_count_ += static_cast<std::uint64_t>(size) * 8U;
        std::size_t offset = 0;
        while (offset < size) {
            const std::size_t available = block_.size() - block_size_;
            const std::size_t copied = (size - offset < available) ? size - offset : available;
            for (std::size_t index = 0; index < copied; ++index) {
                block_[block_size_ + index] = data[offset + index];
            }
            block_size_ += copied;
            offset += copied;
            if (block_size_ == block_.size()) {
                transform(block_.data());
                block_size_ = 0;
            }
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finish() noexcept {
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56U) {
            while (block_size_ < block_.size()) block_[block_size_++] = 0;
            transform(block_.data());
            block_size_ = 0;
        }
        while (block_size_ < 56U) block_[block_size_++] = 0;
        for (std::size_t index = 0; index < 8U; ++index) {
            block_[56U + index] = static_cast<std::uint8_t>(bit_count_ >> (56U - index * 8U));
        }
        transform(block_.data());

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t byte = 0; byte < 4U; ++byte) {
                digest[word * 4U + byte] = static_cast<std::uint8_t>(
                    state_[word] >> (24U - byte * 8U));
            }
        }
        return digest;
    }

private:
    void transform(const std::uint8_t* const block) noexcept {
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t index = 0; index < 16U; ++index) {
            schedule[index] = (static_cast<std::uint32_t>(block[index * 4U]) << 24U) |
                              (static_cast<std::uint32_t>(block[index * 4U + 1U]) << 16U) |
                              (static_cast<std::uint32_t>(block[index * 4U + 2U]) << 8U) |
                              static_cast<std::uint32_t>(block[index * 4U + 3U]);
        }
        for (std::size_t index = 16U; index < schedule.size(); ++index) {
            const std::uint32_t s0 = rotate_right(schedule[index - 15U], 7U) ^
                                     rotate_right(schedule[index - 15U], 18U) ^
                                     (schedule[index - 15U] >> 3U);
            const std::uint32_t s1 = rotate_right(schedule[index - 2U], 17U) ^
                                     rotate_right(schedule[index - 2U], 19U) ^
                                     (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + s0 + schedule[index - 7U] + s1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t index = 0; index < schedule.size(); ++index) {
            const std::uint32_t s1 = rotate_right(e, 6U) ^ rotate_right(e, 11U) ^
                                     rotate_right(e, 25U);
            const std::uint32_t temp1 = h + s1 + choose(e, f, g) +
                                        kRoundConstants[index] + schedule[index];
            const std::uint32_t s0 = rotate_right(a, 2U) ^ rotate_right(a, 13U) ^
                                     rotate_right(a, 22U);
            const std::uint32_t temp2 = s0 + majority(a, b, c);
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::size_t block_size_{0};
    std::uint64_t bit_count_{0};
};

}  // namespace

std::optional<std::string> sha256_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::nullopt;

    Sha256 hasher;
    std::array<std::uint8_t, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            hasher.update(buffer.data(), static_cast<std::size_t>(count));
        }
    }
    if (!input.eof()) return std::nullopt;

    const auto digest = hasher.finish();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t byte : digest) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

std::string sha256_bytes(const std::span<const std::byte> bytes) {
    Sha256 hasher;
    hasher.update(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
    const auto digest = hasher.finish();
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint8_t byte : digest) {
        output << std::setw(2) << static_cast<unsigned int>(byte);
    }
    return output.str();
}

}  // namespace tradutorlinux::util
