#include "sms/storage/FileRepository.hpp"

#include "sms/core/FileSystem.hpp"
#include "sms/storage/Serializer.hpp"

namespace sms::storage {

bool FileRepository::exists() const {
    std::error_code ec;
    return std::filesystem::exists(file_, ec);
}

Database FileRepository::load() const {
    if (!exists()) return Database{};
    return deserialize(fsutil::readAll(file_));
}

void FileRepository::save(const Database& db) const {
    if (file_.has_parent_path()) fsutil::ensureDirectory(file_.parent_path());
    fsutil::atomicWrite(file_, serialize(db));
}

}  // namespace sms::storage
