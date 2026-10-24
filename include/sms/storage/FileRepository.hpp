#pragma once
#include <filesystem>

#include "sms/models/Database.hpp"

namespace sms::storage {

/// Loads and saves the Database as a single file. Saves are atomic (tmp + fsync + rename).
class FileRepository {
public:
    explicit FileRepository(std::filesystem::path file) : file_(std::move(file)) {}

    bool exists() const;
    /// Returns an empty database if the file does not exist; throws DatabaseException if it is
    /// present but unreadable, corrupt, or inconsistent.
    Database load() const;
    void save(const Database& db) const;
    const std::filesystem::path& file() const { return file_; }

private:
    std::filesystem::path file_;
};

}  // namespace sms::storage
