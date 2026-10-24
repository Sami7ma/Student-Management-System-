#pragma once
// Transactional in-memory store on top of FileRepository.
//
// transact() applies a mutation to a COPY of the database, re-validates the invariants, persists
// atomically, and only then swaps the copy in. If the mutation throws, validation fails, or the
// disk write fails, the live data is untouched (strong exception guarantee).
#include <functional>

#include "sms/models/Database.hpp"
#include "sms/storage/FileRepository.hpp"

namespace sms::storage {

class DataStore {
public:
    explicit DataStore(FileRepository repository) : repository_(std::move(repository)) {}

    void load();                       // throws DatabaseException on corruption
    void reload() { load(); }
    const Database& db() const { return db_; }
    void transact(const std::function<void(Database&)>& mutation);
    const FileRepository& repository() const { return repository_; }

private:
    FileRepository repository_;
    Database db_;
};

}  // namespace sms::storage
