#include "sms/storage/DataStore.hpp"

namespace sms::storage {

void DataStore::load() { db_ = repository_.load(); }

void DataStore::transact(const std::function<void(Database&)>& mutation) {
    Database working = db_;
    mutation(working);
    working.validateIntegrity();
    repository_.save(working);
    db_ = std::move(working);
}

}  // namespace sms::storage
