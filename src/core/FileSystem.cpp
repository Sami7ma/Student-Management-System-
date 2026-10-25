#include "sms/core/FileSystem.hpp"

#include <cerrno>
#include <cstring>
#include <fstream>

#include "sms/core/Exceptions.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace sms::fsutil {

void ensureDirectory(const fs::path& dir) {
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) {
        throw DatabaseException("Cannot create directory '" + dir.string() + "': " + ec.message());
    }
}

void restrictPermissions(const fs::path& file) noexcept {
    std::error_code ec;
    fs::permissions(file, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
}

std::string readAll(const fs::path& file) {
    std::error_code ec;
    const auto size = fs::file_size(file, ec);
    if (ec) throw DatabaseException("Cannot stat '" + file.string() + "': " + ec.message());
    if (size > kMaxFileBytes) {
        throw DatabaseException("File '" + file.string() + "' exceeds the 256 MiB safety limit");
    }
    std::ifstream in(file, std::ios::binary);
    if (!in) throw DatabaseException("Cannot open '" + file.string() + "' for reading");
    std::string content(static_cast<std::size_t>(size), '\0');
    if (size > 0 && !in.read(content.data(), static_cast<std::streamsize>(size))) {
        throw DatabaseException("Short read on '" + file.string() + "'");
    }
    return content;
}

void atomicWrite(const fs::path& file, std::string_view content) {
    fs::path tmp = file;
    tmp += ".tmp";
#ifndef _WIN32
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        throw DatabaseException("Cannot create '" + tmp.string() + "': " + std::strerror(errno));
    }
    std::size_t written = 0;
    while (written < content.size()) {
        const ssize_t n = ::write(fd, content.data() + written, content.size() - written);
        if (n < 0) {
            if (errno == EINTR) continue;
            const std::string reason = std::strerror(errno);
            ::close(fd);
            std::error_code ignore;
            fs::remove(tmp, ignore);
            throw DatabaseException("Write to '" + tmp.string() + "' failed: " + reason);
        }
        written += static_cast<std::size_t>(n);
    }
    if (::fsync(fd) != 0) {
        const std::string reason = std::strerror(errno);
        ::close(fd);
        std::error_code ignore;
        fs::remove(tmp, ignore);
        throw DatabaseException("fsync of '" + tmp.string() + "' failed: " + reason);
    }
    ::close(fd);
#else
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) throw DatabaseException("Cannot create '" + tmp.string() + "'");
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) {
            std::error_code ignore;
            fs::remove(tmp, ignore);
            throw DatabaseException("Write to '" + tmp.string() + "' failed");
        }
    }
#endif
    std::error_code ec;
    fs::rename(tmp, file, ec);
#ifdef _WIN32
    if (ec) {  // Older Windows runtimes refuse to rename over an existing file.
        std::error_code rmEc;
        fs::remove(file, rmEc);
        ec.clear();
        fs::rename(tmp, file, ec);
    }
#endif
    if (ec) {
        std::error_code ignore;
        fs::remove(tmp, ignore);
        throw DatabaseException("Atomic rename to '" + file.string() + "' failed: " + ec.message());
    }
    restrictPermissions(file);
}

#ifdef _WIN32
InstanceLock::InstanceLock(const fs::path& lockFile) {
    HANDLE h = CreateFileW(lockFile.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        throw DatabaseException("Another instance is already using this data directory");
    }
    handle_ = h;
}
InstanceLock::~InstanceLock() {
    if (handle_ != nullptr) CloseHandle(static_cast<HANDLE>(handle_));
}
#else
InstanceLock::InstanceLock(const fs::path& lockFile) {
    fd_ = ::open(lockFile.c_str(), O_CREAT | O_RDWR, 0600);
    if (fd_ < 0) {
        throw DatabaseException("Cannot open lock file '" + lockFile.string() + "': " + std::strerror(errno));
    }
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(fd_);
        fd_ = -1;
        throw DatabaseException("Another instance is already using this data directory");
    }
}
InstanceLock::~InstanceLock() {
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
    }
}
#endif

}  // namespace sms::fsutil
