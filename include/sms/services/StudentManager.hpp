#pragma once
#include <optional>
#include <string>
#include <vector>

#include "sms/services/AnalyticsEngine.hpp"

namespace sms {

struct SearchCriteria {
    std::string nameContains;    // case-insensitive partial match
    std::string idContains;      // case-insensitive partial match
    std::string department;      // case-insensitive partial match
    std::optional<double> gpaMin;
    std::optional<double> gpaMax;
    bool includeInactive = false;  // soft-deleted students too
    bool onlyInactive = false;     // ONLY soft-deleted students (recycle bin)
};

enum class SortKey { Name, Id, Gpa, EnrolledOn, Department };

struct SortSpec {
    SortKey key = SortKey::Name;
    bool ascending = true;
};

class StudentManager {
public:
    StudentManager(storage::DataStore& store, storage::AuditLogger& audit) : store_(store), audit_(audit) {}

    Student create(const User& actor, Student draft);
    /// Replaces the editable fields of an existing, active student (matched by draft.id).
    Student update(const User& actor, const Student& draft);
    void softDelete(const User& actor, std::string_view id);
    void restore(const User& actor, std::string_view id);
    /// Removes the student, their enrollments and any linked login accounts. Irreversible.
    void hardDelete(const User& actor, std::string_view id);

    std::optional<Student> find(const User& actor, std::string_view id) const;
    std::vector<StudentSummary> search(const User& actor, const SearchCriteria& criteria, const SortSpec& sort) const;
    std::vector<std::string> departments(const User& actor) const;

private:
    storage::DataStore& store_;
    storage::AuditLogger& audit_;
};

}  // namespace sms
