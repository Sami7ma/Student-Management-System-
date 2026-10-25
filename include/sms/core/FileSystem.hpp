#pragma once
// File-system primitives: crash-safe atomic writes, bounded reads, and a cross-process
// instance lock that prevents two copies of the program from corrupting the same database.
#include <filesystem>
#include <string>
#include <string_view>

namespace sms::fsutil {

constexpr std::uintmax_t kMaxFileBytes = 256ULL * 1024 * 1024;

void ensureDirectory(const std::filesystem::path& dir);
/// Best-effort owner-only permissions (0600). No-op on platforms without POSIX modes.
void restrictPermissions(const std::filesystem::path& file) noexcept;
std::string readAll(const std::filesystem::path& file);
/// Writes to "<file>.tmp", flushes to disk, then atomically renames over `file`.
void atomicWrite(const std::filesystem::path& file, std::string_view content);

/// RAII advisory lock. Throws DatabaseException if another process already holds it.
class InstanceLock {
public:
    explicit InstanceLock(const std::filesystem::path& lockFile);
    ~InstanceLock();
    InstanceLock(const InstanceLock&) = delete;
    InstanceLock& operator=(const InstanceLock&) = delete;

private:
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

}  // namespace sms::fsutil
