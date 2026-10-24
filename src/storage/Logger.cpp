#include "sms/storage/Logger.hpp"

#include <fstream>

#include "sms/core/Crypto.hpp"
#include "sms/core/FileSystem.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"

namespace sms::storage {

namespace {
constexpr const char* kGenesis = "GENESIS";

const char* levelName(AuditLevel l) {
    switch (l) {
        case AuditLevel::Info: return "INFO";
        case AuditLevel::Warning: return "WARN";
        case AuditLevel::Critical: return "CRIT";
    }
    return "INFO";
}

/// Splits "<body> | chain:<hash>" into its parts. Returns false when no chain suffix exists.
bool splitChain(const std::string& line, std::string& body, std::string& chain) {
    const std::string marker = " | chain:";
    const std::size_t p = line.rfind(marker);
    if (p == std::string::npos) return false;
    body = line.substr(0, p);
    chain = line.substr(p + marker.size());
    return true;
}
}  // namespace

std::string AuditLogger::chainOf(const std::string& previous, const std::string& line) {
    return crypto::sha256Hex(previous + "|" + line).substr(0, 32);
}

AuditLogger::AuditLogger(std::filesystem::path file) : file_(std::move(file)) {
    try {
        if (file_.has_parent_path()) fsutil::ensureDirectory(file_.parent_path());
        lastChain_ = lastChain();
    } catch (...) {
        healthy_ = false;
        lastChain_ = kGenesis;
    }
}

std::string AuditLogger::lastChain() const {
    std::ifstream in(file_);
    if (!in) return kGenesis;
    std::string line;
    std::string last = kGenesis;
    std::string body;
    std::string chain;
    while (std::getline(in, line)) {
        if (splitChain(line, body, chain)) last = chain;
    }
    return last;
}

void AuditLogger::log(AuditLevel level, std::string_view event, std::string_view user,
                      std::string_view detail) noexcept {
    try {
        std::lock_guard<std::mutex> guard(mutex_);
        const std::string body = timeutil::nowTimestamp() + " | " + levelName(level) + " | " +
                                 text::sanitizeForLog(event, 40) + " | " +
                                 (user.empty() ? "-" : text::sanitizeForLog(user, 40)) + " | " +
                                 text::sanitizeForLog(detail, 300);
        const std::string chain = chainOf(lastChain_, body);
        const bool isNew = !std::filesystem::exists(file_);
        std::ofstream out(file_, std::ios::app);  // append-only: existing bytes are never rewritten
        if (!out) {
            healthy_ = false;
            return;
        }
        out << body << " | chain:" << chain << '\n';
        out.flush();
        if (!out) {
            healthy_ = false;
            return;
        }
        lastChain_ = chain;
        if (isNew) fsutil::restrictPermissions(file_);
    } catch (...) {
        healthy_ = false;
    }
}

AuditVerification AuditLogger::verify() const {
    std::lock_guard<std::mutex> guard(mutex_);
    AuditVerification result;
    std::ifstream in(file_);
    if (!in) return result;  // no log yet == nothing to tamper with
    std::string previous = kGenesis;
    std::string line;
    while (std::getline(in, line)) {
        ++result.lines;
        std::string body;
        std::string chain;
        if (!splitChain(line, body, chain) || chainOf(previous, body) != chain) {
            result.ok = false;
            result.firstBadLine = result.lines;
            return result;
        }
        previous = chain;
    }
    return result;
}

std::vector<std::string> AuditLogger::tail(std::size_t count) const {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<std::string> all;
    std::ifstream in(file_);
    std::string line;
    while (std::getline(in, line)) {
        all.push_back(line);
        if (all.size() > count * 2 + 1000) all.erase(all.begin(), all.begin() + 500);  // bound memory
    }
    if (all.size() > count) all.erase(all.begin(), all.end() - static_cast<std::ptrdiff_t>(count));
    return all;
}

}  // namespace sms::storage
