#include "sms/models/Database.hpp"

#include <algorithm>
#include <set>
#include <tuple>

#include "sms/core/Exceptions.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Rules.hpp"

namespace sms {

namespace {
template <typename Container, typename Pred>
auto findIf(Container& c, Pred p) -> decltype(&*c.begin()) {
    auto it = std::find_if(c.begin(), c.end(), p);
    return it == c.end() ? nullptr : &*it;
}
}  // namespace

const Student* Database::findStudent(std::string_view id) const {
    return findIf(students, [&](const Student& s) { return s.id == id; });
}
Student* Database::findStudent(std::string_view id) {
    return findIf(students, [&](const Student& s) { return s.id == id; });
}
const Course* Database::findCourse(std::string_view code) const {
    return findIf(courses, [&](const Course& c) { return c.code == code; });
}
Course* Database::findCourse(std::string_view code) {
    return findIf(courses, [&](const Course& c) { return c.code == code; });
}
const User* Database::findUser(std::string_view username) const {
    return findIf(users, [&](const User& u) { return u.username == username; });
}
User* Database::findUser(std::string_view username) {
    return findIf(users, [&](const User& u) { return u.username == username; });
}
const Enrollment* Database::findEnrollment(std::string_view sid, std::string_view code,
                                           std::string_view term) const {
    return findIf(enrollments, [&](const Enrollment& e) {
        return e.studentId == sid && e.courseCode == code && e.term == term;
    });
}
Enrollment* Database::findEnrollment(std::string_view sid, std::string_view code, std::string_view term) {
    return findIf(enrollments, [&](const Enrollment& e) {
        return e.studentId == sid && e.courseCode == code && e.term == term;
    });
}

void Database::validateIntegrity() const {
    const std::string today = timeutil::today();
    std::set<std::string> studentIds;
    std::set<std::string> courseCodes;
    std::set<std::string> usernames;
    std::set<std::tuple<std::string, std::string, std::string>> enrollmentKeys;

    for (const auto& s : students) {
        if (auto e = rules::validateStudent(s, today)) {
            throw DatabaseException("Invalid student '" + s.id + "': " + *e);
        }
        if (!studentIds.insert(s.id).second) throw DatabaseException("Duplicate student ID '" + s.id + "'");
    }
    for (const auto& c : courses) {
        if (auto e = rules::validateCourse(c)) throw DatabaseException("Invalid course '" + c.code + "': " + *e);
        if (!courseCodes.insert(c.code).second) throw DatabaseException("Duplicate course code '" + c.code + "'");
    }
    for (const auto& u : users) {
        if (auto e = rules::validateUser(u)) throw DatabaseException("Invalid user '" + u.username + "': " + *e);
        if (!usernames.insert(u.username).second) throw DatabaseException("Duplicate username '" + u.username + "'");
        if (u.role == Role::Student && studentIds.count(u.linkedStudentId) == 0) {
            throw DatabaseException("User '" + u.username + "' is linked to unknown student '" +
                                    u.linkedStudentId + "'");
        }
    }
    for (const auto& e : enrollments) {
        if (auto err = rules::validateEnrollment(e)) {
            throw DatabaseException("Invalid enrollment " + e.studentId + "/" + e.courseCode + ": " + *err);
        }
        if (studentIds.count(e.studentId) == 0) {
            throw DatabaseException("Enrollment references unknown student '" + e.studentId + "'");
        }
        if (courseCodes.count(e.courseCode) == 0) {
            throw DatabaseException("Enrollment references unknown course '" + e.courseCode + "'");
        }
        if (!enrollmentKeys.insert({e.studentId, e.courseCode, e.term}).second) {
            throw DatabaseException("Duplicate enrollment " + e.studentId + "/" + e.courseCode + "/" + e.term);
        }
    }
}

}  // namespace sms
