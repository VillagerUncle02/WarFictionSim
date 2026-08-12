// sim/src/sha256.cpp
//
// T016：SHA-256 实现（FIPS 180-4，自实现、无外部依赖）。
//
// 实现策略：按 FIPS 180-4 §5.1.1 填充（0x80 + 零 + 64 位大端位长），
// 每 64 字节一个消息块扩展 64 个调度字，然后执行 64 轮压缩；全部使用
// 定宽无符号整数运算，跨编译器/平台结果一致（宪法第 7 条）。位长写入
// 使用显式字节循环，避免依赖宿主字节序。输入按 uint64 计数，仅支持
// < 2^61 字节的输入（存档/状态远低于该上限），不产生回绕。

#include "wfs/sim/sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace wfs::sim {

namespace {

constexpr std::size_t kBlockSize = 64U;             // 消息块字节数。
constexpr std::size_t kMessageSizeFieldBytes = 8U;  // 位长字段字节数。
constexpr std::size_t kScheduleSeedWords = 16U;     // 消息块直接填充的调度字。
constexpr std::size_t kScheduleSize = 64U;          // 完整调度字数。
constexpr std::size_t kDigestWordCount = 8U;        // 摘要字数量。
constexpr std::size_t kBytesPerWord = 4U;           // 每字字节数。
constexpr std::uint32_t kByteBits = 8U;             // 每字节位宽。
constexpr std::uint8_t kPaddingByte = 0x80U;        // FIPS 180-4 填充首字节。
constexpr std::uint8_t kByteMask = 0xFFU;           // 字节掩码。
constexpr std::uint8_t kNibbleMask = 0x0FU;         // 十六进制低半字节掩码。
constexpr std::uint32_t kWordBits = 32U;            // SHA-256 字宽。

// 压缩轮工作寄存器在 hash 数组中的下标（FIPS 180-4 命名 a..h）。
constexpr std::size_t kWordA = 0U;
constexpr std::size_t kWordB = 1U;
constexpr std::size_t kWordC = 2U;
constexpr std::size_t kWordD = 3U;
constexpr std::size_t kWordE = 4U;
constexpr std::size_t kWordF = 5U;
constexpr std::size_t kWordG = 6U;
constexpr std::size_t kWordH = 7U;

// 大端字内 4 个字节的位偏移（FIPS 180-4 大端字布局：最高字节在前）。
constexpr std::array<std::uint32_t, kBytesPerWord> kByteShiftBits = {24U, 16U, 8U, 0U};

// FIPS 180-4 定义的循环右移位数：调度扩展 σ0/σ1 与压缩 Σ0/Σ1。
constexpr std::uint32_t kScheduleSigma0Shift1 = 7U;
constexpr std::uint32_t kScheduleSigma0Shift2 = 18U;
constexpr std::uint32_t kScheduleSigma0Shift3 = 3U;
constexpr std::uint32_t kScheduleSigma1Shift1 = 17U;
constexpr std::uint32_t kScheduleSigma1Shift2 = 19U;
constexpr std::uint32_t kScheduleSigma1Shift3 = 10U;
constexpr std::uint32_t kCompressionSigma0Shift1 = 2U;
constexpr std::uint32_t kCompressionSigma0Shift2 = 13U;
constexpr std::uint32_t kCompressionSigma0Shift3 = 22U;
constexpr std::uint32_t kCompressionSigma1Shift1 = 6U;
constexpr std::uint32_t kCompressionSigma1Shift2 = 11U;
constexpr std::uint32_t kCompressionSigma1Shift3 = 25U;

// 消息调度扩展的回看偏移（FIPS 180-4 §6.2.2：w[i] 由 w[i-16]/w[i-15]/
// w[i-7]/w[i-2] 组合）。
constexpr std::size_t kScheduleOffset16 = 16U;
constexpr std::size_t kScheduleOffset15 = 15U;
constexpr std::size_t kScheduleOffset7 = 7U;
constexpr std::size_t kScheduleOffset2 = 2U;

// FIPS 180-4 §4.2.2 的 64 个轮常量（前 64 个素数的立方根小数部分）。
constexpr std::array<std::uint32_t, kScheduleSize> kSha256Constants = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL,
};

// FIPS 180-4 §5.3.3 初始哈希值（前 8 个素数的平方根小数部分）。
constexpr std::array<std::uint32_t, kDigestWordCount> kInitialHash = {
    0x6a09e667UL, 0xbb67ae85UL, 0x3c6ef372UL, 0xa54ff53aUL, 0x510e527fUL, 0x9b05688cUL, 0x1f83d9abUL, 0x5be0cd19UL,
};

std::uint32_t RotateRight(std::uint32_t value, std::uint32_t bits) noexcept {
    return (value >> bits) | (value << (kWordBits - bits));
}

// 从消息块偏移处读取大端 32 位字（不依赖宿主字节序）。
std::uint32_t ReadBigEndianWord(const std::vector<std::uint8_t>& message, std::size_t offset) noexcept {
    std::uint32_t word = 0U;
    for (std::size_t i = 0U; i < kBytesPerWord; ++i) {
        word = (word << kByteBits) | message[offset + i];
    }
    return word;
}

}  // namespace

Sha256Digest sha256(const std::string& data) {
    std::vector<std::uint8_t> message(data.begin(), data.end());
    message.push_back(kPaddingByte);
    while (message.size() % kBlockSize != kBlockSize - kMessageSizeFieldBytes) {
        message.push_back(0x00U);
    }
    const std::uint64_t bit_length = static_cast<std::uint64_t>(data.size()) * kByteBits;
    for (std::size_t i = 0U; i < kMessageSizeFieldBytes; ++i) {
        const std::size_t shift = kByteBits * (kMessageSizeFieldBytes - 1U - i);
        message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & kByteMask));
    }

    std::array<std::uint32_t, kDigestWordCount> hash = kInitialHash;
    for (std::size_t block = 0U; block < message.size(); block += kBlockSize) {
        std::array<std::uint32_t, kScheduleSize> words{};
        for (std::size_t i = 0U; i < kScheduleSeedWords; ++i) {
            words[i] = ReadBigEndianWord(message, block + (i * kBytesPerWord));
        }
        for (std::size_t i = kScheduleSeedWords; i < kScheduleSize; ++i) {
            const std::uint32_t sigma0 = RotateRight(words[i - kScheduleOffset15], kScheduleSigma0Shift1) ^
                                         RotateRight(words[i - kScheduleOffset15], kScheduleSigma0Shift2) ^
                                         (words[i - kScheduleOffset15] >> kScheduleSigma0Shift3);
            const std::uint32_t sigma1 = RotateRight(words[i - kScheduleOffset2], kScheduleSigma1Shift1) ^
                                         RotateRight(words[i - kScheduleOffset2], kScheduleSigma1Shift2) ^
                                         (words[i - kScheduleOffset2] >> kScheduleSigma1Shift3);
            words[i] = words[i - kScheduleOffset16] + sigma0 + words[i - kScheduleOffset7] + sigma1;
        }

        std::uint32_t work_a = hash[kWordA];
        std::uint32_t work_b = hash[kWordB];
        std::uint32_t work_c = hash[kWordC];
        std::uint32_t work_d = hash[kWordD];
        std::uint32_t work_e = hash[kWordE];
        std::uint32_t work_f = hash[kWordF];
        std::uint32_t work_g = hash[kWordG];
        std::uint32_t work_h = hash[kWordH];
        for (std::size_t i = 0U; i < kScheduleSize; ++i) {
            const std::uint32_t sum1 = RotateRight(work_e, kCompressionSigma1Shift1) ^
                                       RotateRight(work_e, kCompressionSigma1Shift2) ^
                                       RotateRight(work_e, kCompressionSigma1Shift3);
            const std::uint32_t choose = (work_e & work_f) ^ ((~work_e) & work_g);
            const std::uint32_t temp1 = work_h + sum1 + choose + kSha256Constants[i] + words[i];
            const std::uint32_t sum0 = RotateRight(work_a, kCompressionSigma0Shift1) ^
                                       RotateRight(work_a, kCompressionSigma0Shift2) ^
                                       RotateRight(work_a, kCompressionSigma0Shift3);
            const std::uint32_t majority = (work_a & work_b) ^ (work_a & work_c) ^ (work_b & work_c);
            const std::uint32_t temp2 = sum0 + majority;
            work_h = work_g;
            work_g = work_f;
            work_f = work_e;
            work_e = work_d + temp1;
            work_d = work_c;
            work_c = work_b;
            work_b = work_a;
            work_a = temp1 + temp2;
        }
        hash[kWordA] += work_a;
        hash[kWordB] += work_b;
        hash[kWordC] += work_c;
        hash[kWordD] += work_d;
        hash[kWordE] += work_e;
        hash[kWordF] += work_f;
        hash[kWordG] += work_g;
        hash[kWordH] += work_h;
    }

    Sha256Digest digest{};
    for (std::size_t i = 0U; i < hash.size(); ++i) {
        for (std::size_t byte_index = 0U; byte_index < kBytesPerWord; ++byte_index) {
            digest[(i * kBytesPerWord) + byte_index] = static_cast<std::uint8_t>(hash[i] >> kByteShiftBits[byte_index]);
        }
    }
    return digest;
}

std::string sha256_hex(const std::string& data) {
    return sha256_hex(sha256(data));
}

std::string sha256_hex(const Sha256Digest& digest) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(kSha256DigestSize * 2U);
    for (const std::uint8_t byte : digest) {
        out.push_back(kHexDigits[byte >> 4U]);
        out.push_back(kHexDigits[byte & kNibbleMask]);
    }
    return out;
}

}  // namespace wfs::sim
