#pragma once
#include <optional>
#include <string>
#include <vector>

#include "sms/models/Enrollment.hpp"
#include "sms/storage/DataStore.hpp"
#include "sms/storage/Logger.hpp"

namespace sms {

class EnrollmentManager {
public:
    EnrollmentManager(storage::DataStore& store, storage::AuditLogger& audit) : store_(store), audit_(audit) {}

    /// Enrols an ACTIVE student in an ACTIVE course for `term`; optionally records a mark.
    Enrollment enroll(const User& actor, std::string_view studentId, std::string_view courseCode,
                      std::string_view term, std::optional<double> mark = std::nullopt);
    void withdraw(const User& actor, std::string_view studentId, std::string_view courseCode, std::string_view term);
    void recordMark(const User& actor, std::string_view studentId, std::string_view courseCode,
                    std::string_view term, double mark);
    void clearMark(const User& actor, std::string_view studentId, std::string_view courseCode, std::string_view term);

    std::vector<Enrollment> forStudent(const User& actor, std::string_view studentId) const;
    std::vector<Enrollment> forCourse(const User& actor, std::string_view courseCode) const;

private:
    storage::DataStore& store_;
    storage::AuditLogger& audit_;
};

}  // namespace sms
