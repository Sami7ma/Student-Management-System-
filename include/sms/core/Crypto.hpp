#pragma once
// Self-contained SHA-256, HMAC-SHA-256 and PBKDF2-HMAC-SHA-256 (no external dependencies),
// plus an OS-backed random source and a constant-time comparison.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sms::crypto {

using Bytes = std::vector<std::uint8_t>;
using Digest = std::array<std::uint8_t, 32>;

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(const std::uint8_t* data, std::size_t len);
    void update(std::string_view text);
    /// Finalizes and returns the digest. Call reset() before reusing the object.
    Digest finish();

private:
    void compress(const std::uint8_t* block);
    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t bufferLen_ = 0;
    std::uint64_t totalLen_ = 0;
};

Digest sha256(std::string_view data);
std::string sha256Hex(std::string_view data);

/// HMAC-SHA-256 with precomputed pad states so PBKDF2 loops stay cheap.
class HmacSha256 {
public:
    HmacSha256(const std::uint8_t* key, std::size_t keyLen);
    Digest compute(const std::uint8_t* msg, std::size_t len) const;

private:
    Sha256 inner_;
    Sha256 outer_;
};

Bytes pbkdf2HmacSha256(std::string_view password, const Bytes& salt, std::uint32_t iterations,
                       std::size_t derivedKeyLen);

std::string toHex(const std::uint8_t* data, std::size_t len);
std::string toHex(const Bytes& bytes);
std::optional<Bytes> fromHex(std::string_view hex);

/// Cryptographically secure random bytes (/dev/urandom, or std::random_device elsewhere).
Bytes randomBytes(std::size_t count);

bool constantTimeEquals(const Bytes& a, const Bytes& b);

}  // namespace sms::crypto
