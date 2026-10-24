#include "sms/services/BackupService.hpp"

#include <algorithm>

#include "sms/core/Exceptions.hpp"
#include "sms/core/FileSystem.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"
#include "sms/services/AccessPolicy.hpp"
#include "sms/storage/Serializer.hpp"

namespace fs = std::filesystem;

namespace sms {

namespace {
std::string safeReason(std::string_view reason) {
    std::string out;
    for (char c : reason) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-') out.push_back(c);
        else if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
        else out.push_back('-');
    }
    return out.empty() ? "manual" : out.substr(0, 24);
}
}  // namespace

std::vector<BackupInfo> BackupService::listUnchecked() const {
    std::vector<BackupInfo> out;
    std::error_code ec;
    if (!fs::exists(directory_, ec)) return out;
    for (const auto& entry : fs::directory_iterator(directory_, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        const std::string name = entry.path().filename().string();
        if (name.rfind("sms_", 0) != 0 || entry.path().extension() != ".db") continue;
        out.push_back({name, entry.file_size(ec)});
    }
    std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) { return a.name > b.name; });  // newest first
    return out;
}

void BackupService::prune() {
    auto all = listUnchecked();
    for (std::size_t i = keep_; i < all.size(); ++i) {
        std::error_code ec;
        fs::remove(directory_ / all[i].name, ec);
    }
}

fs::path BackupService::snapshot(std::string_view actorName, std::string_view reason) {
    const fs::path source = store_.repository().file();
    std::error_code ec;
    if (!fs::exists(source, ec)) return {};
    fsutil::ensureDirectory(directory_);
    const std::string content = fsutil::readAll(source);
    const std::string stem = "sms_" + timeutil::fileStamp() + "_" + safeReason(reason);
    fs::path target = directory_ / (stem + ".db");
    for (int n = 2; fs::exists(target, ec) && n < 1000; ++n) target = directory_ / (stem + "_" + std::to_string(n) + ".db");
    fsutil::atomicWrite(target, content);
    audit_.info("BACKUP_CREATED", actorName, "file=" + target.filename().string() + " reason=" + std::string(reason));
    prune();
    return target;
}

fs::path BackupService::createManual(const User& actor) {
    AccessPolicy::require(actor, Permission::ManageBackups, audit_);
    const fs::path p = snapshot(actor.username, "manual");
    if (p.empty()) throw DatabaseException("There is no database file to back up yet.");
    return p;
}

std::vector<BackupInfo> BackupService::list(const User& actor) const {
    AccessPolicy::require(actor, Permission::ManageBackups, audit_);
    return listUnchecked();
}

void BackupService::restore(const User& actor, std::string_view backupName) {
    AccessPolicy::require(actor, Permission::ManageBackups, audit_);
    const std::string name(backupName);
    // Path-traversal guard: must be a bare file name that exists in the backup directory listing.
    const auto all = listUnchecked();
    const bool known = std::any_of(all.begin(), all.end(), [&](const BackupInfo& b) { return b.name == name; });
    if (!known || fs::path(name).filename().string() != name) {
        audit_.warn("RESTORE_REJECTED", actor.username, "unknown backup name");
        throw NotFoundException("Backup not found.");
    }
    const std::string content = fsutil::readAll(directory_ / name);
    (void)storage::deserialize(content);  // throws DatabaseException when corrupt - before touching live data
    snapshot(actor.username, "pre-restore");
    fsutil::atomicWrite(store_.repository().file(), content);
    store_.reload();
    audit_.critical("BACKUP_RESTORED", actor.username, "file=" + name);
}

std::string BackupService::recoverLatestValid(std::string_view reason) {
    for (const auto& b : listUnchecked()) {
        try {
            const std::string content = fsutil::readAll(directory_ / b.name);
            (void)storage::deserialize(content);
            fsutil::atomicWrite(store_.repository().file(), content);
            store_.reload();
            audit_.critical("BACKUP_RECOVERED", "system", "file=" + b.name + " reason=" + std::string(reason));
            return b.name;
        } catch (const DatabaseException&) {
            audit_.warn("BACKUP_SKIPPED", "system", "unusable snapshot " + b.name);
        }
    }
    return {};
}

}  // namespace sms
