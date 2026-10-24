#pragma once
#include <optional>
#include <string>
#include <vector>

#include "sms/models/Course.hpp"
#include "sms/storage/DataStore.hpp"
#include "sms/storage/Logger.hpp"

namespace sms {

class CourseManager {
public:
    CourseManager(storage::DataStore& store, storage::AuditLogger& audit) : store_(store), audit_(audit) {}

    Course create(const User& actor, Course draft);
    Course update(const User& actor, const Course& draft);       // matched by code
    void setActive(const User& actor, std::string_view code, bool active);  // soft delete / restore
    /// Removes the course AND every enrollment in it. Irreversible (Admin only).
    std::size_t hardDelete(const User& actor, std::string_view code);

    std::optional<Course> find(const User& actor, std::string_view code) const;
    std::vector<Course> list(const User& actor, bool includeInactive) const;

private:
    storage::DataStore& store_;
    storage::AuditLogger& audit_;
};

}  // namespace sms
