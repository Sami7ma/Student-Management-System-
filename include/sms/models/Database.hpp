#pragma once
// The in-memory aggregate that is persisted as one unit.
#include <string_view>
#include <vector>

#include "sms/models/Course.hpp"
#include "sms/models/Enrollment.hpp"
#include "sms/models/Student.hpp"
#include "sms/models/User.hpp"

namespace sms {

struct Database {
    std::vector<User> users;
    std::vector<Student> students;
    std::vector<Course> courses;
    std::vector<Enrollment> enrollments;

    const Student* findStudent(std::string_view id) const;
    Student* findStudent(std::string_view id);
    const Course* findCourse(std::string_view code) const;
    Course* findCourse(std::string_view code);
    const User* findUser(std::string_view username) const;
    User* findUser(std::string_view username);
    const Enrollment* findEnrollment(std::string_view studentId, std::string_view courseCode,
                                     std::string_view term) const;
    Enrollment* findEnrollment(std::string_view studentId, std::string_view courseCode,
                               std::string_view term);

    /// Verifies every entity against the domain rules, key uniqueness and referential
    /// integrity. Throws DatabaseException describing the first violation.
    void validateIntegrity() const;
};

}  // namespace sms
