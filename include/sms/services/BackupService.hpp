#pragma once
// Snapshot backups of the database file plus validated restore.
#include <filesystem>
#include <string>
#include <vector>

#include "sms/storage/DataStore.hpp"
#include "sms/storage/Logger.hpp"

namespace sms {

struct BackupInfo {
    std::string name;       // file name inside the backup directory
    std::uintmax_t bytes = 0;
};

class BackupService {
public:
    BackupService(storage::DataStore& store, storage::AuditLogger& audit, std::filesystem::path directory,
                  std::size_t keep = 20)
        : store_(store), audit_(audit), directory_(std::move(directory)), keep_(keep) {}

    /// Internal snapshot (startup / before destructive operations). Returns an empty path if there
    /// is no database file yet. Never RBAC-checked: it is triggered by the application itself.
    std::filesystem::path snapshot(std::string_view actorName, std::string_view reason);
    std::filesystem::path createManual(const User& actor);
    std::vector<BackupInfo> list(const User& actor) const;
    /// Validates the snapshot (checksum + schema), snapshots the current file, then replaces it.
    void restore(const User& actor, std::string_view backupName);

    /// Startup recovery (no logged-in user yet): restores the newest snapshot that passes full
    /// validation. Returns its file name, or an empty string if none is usable.
    std::string recoverLatestValid(std::string_view reason);

    const std::filesystem::path& directory() const { return directory_; }

private:
    void prune();
    std::vector<BackupInfo> listUnchecked() const;

    storage::DataStore& store_;
    storage::AuditLogger& audit_;
    std::filesystem::path directory_;
    std::size_t keep_;
};

}  // namespace sms
