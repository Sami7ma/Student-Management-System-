#include "sms/core/Crypto.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>

#include "sms/core/Exceptions.hpp"

namespace sms::crypto {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u,
    0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu,
    0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu,
    0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau, 0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu,
    0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u, 0x19a4c116u,
    0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u,
    0xc67178f2u};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32U - n)); }

}  // namespace

void Sha256::reset() {
    h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
          0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    bufferLen_ = 0;
    totalLen_ = 0;
}

void Sha256::compress(const std::uint8_t* block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
    std::uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kRoundConstants[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
    h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(const std::uint8_t* data, std::size_t len) {
    totalLen_ += len;
    while (len > 0) {
        const std::size_t take = std::min(len, buffer_.size() - bufferLen_);
        std::memcpy(buffer_.data() + bufferLen_, data, take);
        bufferLen_ += take;
        data += take;
        len -= take;
        if (bufferLen_ == buffer_.size()) {
            compress(buffer_.data());
            bufferLen_ = 0;
        }
    }
}

void Sha256::update(std::string_view text) {
    update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

Digest Sha256::finish() {
    const std::uint64_t bitLen = totalLen_ * 8U;
    buffer_[bufferLen_++] = 0x80;
    if (bufferLen_ > 56) {
        std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(bufferLen_), buffer_.end(), 0);
        compress(buffer_.data());
        bufferLen_ = 0;
    }
    std::fill(buffer_.begin() + static_cast<std::ptrdiff_t>(bufferLen_), buffer_.end(), 0);
    for (std::size_t i = 0; i < 8; ++i) {
        buffer_[63 - i] = static_cast<std::uint8_t>((bitLen >> (8U * i)) & 0xFFU);
    }
    compress(buffer_.data());
    Digest out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[i * 4] = static_cast<std::uint8_t>(h_[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(h_[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(h_[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(h_[i]);
    }
    return out;
}

Digest sha256(std::string_view data) {
    Sha256 h;
    h.update(data);
    return h.finish();
}

std::string sha256Hex(std::string_view data) {
    const Digest d = sha256(data);
    return toHex(d.data(), d.size());
}

HmacSha256::HmacSha256(const std::uint8_t* key, std::size_t keyLen) {
    std::array<std::uint8_t, 64> block{};
    if (keyLen > block.size()) {
        Sha256 h;
        h.update(key, keyLen);
        const Digest d = h.finish();
        std::copy(d.begin(), d.end(), block.begin());
    } else if (keyLen > 0) {
        std::memcpy(block.data(), key, keyLen);
    }
    std::array<std::uint8_t, 64> ipad{};
    std::array<std::uint8_t, 64> opad{};
    for (std::size_t i = 0; i < block.size(); ++i) {
        ipad[i] = static_cast<std::uint8_t>(block[i] ^ 0x36U);
        opad[i] = static_cast<std::uint8_t>(block[i] ^ 0x5cU);
    }
    inner_.update(ipad.data(), ipad.size());
    outer_.update(opad.data(), opad.size());
}

Digest HmacSha256::compute(const std::uint8_t* msg, std::size_t len) const {
    Sha256 in = inner_;
    in.update(msg, len);
    const Digest innerDigest = in.finish();
    Sha256 out = outer_;
    out.update(innerDigest.data(), innerDigest.size());
    return out.finish();
}

Bytes pbkdf2HmacSha256(std::string_view password, const Bytes& salt, std::uint32_t iterations,
                       std::size_t derivedKeyLen) {
    if (iterations == 0 || derivedKeyLen == 0) {
        throw ValidationException("PBKDF2 requires iterations > 0 and a positive key length");
    }
    const HmacSha256 prf(reinterpret_cast<const std::uint8_t*>(password.data()), password.size());
    Bytes result;
    result.reserve(derivedKeyLen);
    for (std::uint32_t blockIndex = 1; result.size() < derivedKeyLen; ++blockIndex) {
        Bytes seed = salt;
        seed.push_back(static_cast<std::uint8_t>(blockIndex >> 24));
        seed.push_back(static_cast<std::uint8_t>(blockIndex >> 16));
        seed.push_back(static_cast<std::uint8_t>(blockIndex >> 8));
        seed.push_back(static_cast<std::uint8_t>(blockIndex));
        Digest u = prf.compute(seed.data(), seed.size());
        Digest t = u;
        for (std::uint32_t i = 1; i < iterations; ++i) {
            u = prf.compute(u.data(), u.size());
            for (std::size_t j = 0; j < t.size(); ++j) t[j] = static_cast<std::uint8_t>(t[j] ^ u[j]);
        }
        const std::size_t take = std::min(t.size(), derivedKeyLen - result.size());
        result.insert(result.end(), t.begin(), t.begin() + static_cast<std::ptrdiff_t>(take));
    }
    return result;
}

std::string toHex(const std::uint8_t* data, std::size_t len) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(len * 2);
    for (std::size_t i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0FU]);
    }
    return out;
}

std::string toHex(const Bytes& bytes) { return toHex(bytes.data(), bytes.size()); }

std::optional<Bytes> fromHex(std::string_view hex) {
    if (hex.size() % 2 != 0) return std::nullopt;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    Bytes out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nibble(hex[i]);
        const int lo = nibble(hex[i + 1]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return out;
}

Bytes randomBytes(std::size_t count) {
    Bytes out(count);
#if defined(__unix__) || defined(__APPLE__)
    std::ifstream urandom("/dev/urandom", std::ios::binary);
    if (urandom && urandom.read(reinterpret_cast<char*>(out.data()),
                                static_cast<std::streamsize>(count))) {
        return out;
    }
#endif
    // Fallback. On very old MinGW toolchains std::random_device can be deterministic;
    // modern MSVC / MinGW-w64 builds use the OS CSPRNG.
    std::random_device rd;
    for (auto& b : out) b = static_cast<std::uint8_t>(rd() & 0xFFU);
    return out;
}

bool constantTimeEquals(const Bytes& a, const Bytes& b) {
    if (a.size() != b.size()) return false;
    std::uint8_t diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) diff = static_cast<std::uint8_t>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

}  // namespace sms::crypto
