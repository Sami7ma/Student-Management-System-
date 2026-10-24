#pragma once
// Append-only, tamper-evident security audit log.
//
// Each line:  <timestamp> | <LEVEL> | <EVENT> | <user> | <detail> | chain:<32 hex>
// where chain = first 32 hex chars of SHA-256(previousChain + "|" + lineWithoutChain). Editing,
// deleting or re-ordering any earlier line breaks every later chain value; verify() detects it.
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace sms::storage {

enum class AuditLevel { Info, Warning, Critical };

struct AuditVerification {
    bool ok = true;
    std::size_t lines = 0;
    std::size_t firstBadLine = 0;  // 1-based, 0 if ok
};

class AuditLogger {
public:
    explicit AuditLogger(std::filesystem::path file);

    /// Never throws: audit-log I/O problems must not crash the application, but they are
    /// remembered and surfaced through healthy() so the UI can warn the operator.
    void log(AuditLevel level, std::string_view event, std::string_view user, std::string_view detail) noexcept;
    void info(std::string_view event, std::string_view user, std::string_view detail) noexcept {
        log(AuditLevel::Info, event, user, detail);
    }
    void warn(std::string_view event, std::string_view user, std::string_view detail) noexcept {
        log(AuditLevel::Warning, event, user, detail);
    }
    void critical(std::string_view event, std::string_view user, std::string_view detail) noexcept {
        log(AuditLevel::Critical, event, user, detail);
    }

    bool healthy() const { return healthy_; }
    AuditVerification verify() const;
    std::vector<std::string> tail(std::size_t count) const;
    const std::filesystem::path& file() const { return file_; }

private:
    static std::string chainOf(const std::string& previous, const std::string& line);
    std::string lastChain() const;

    std::filesystem::path file_;
    mutable std::mutex mutex_;
    std::string lastChain_;
    bool healthy_ = true;
};

}  // namespace sms::storage
