#include "sms/services/EnrollmentManager.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "sms/core/Exceptions.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Rules.hpp"
#include "sms/services/AccessPolicy.hpp"

namespace sms {

namespace {
double round2(double v) { return std::round(v * 100.0) / 100.0; }

struct Key {
    std::string student, course, term;
};

Key makeKey(std::string_view s, std::string_view c, std::string_view t) {
    Key k{rules::normalizeStudentId(s), rules::normalizeCourseCode(c), text::trim(t)};
    if (auto e = rules::checkTerm(k.term)) throw ValidationException(*e);
    return k;
}
}  // namespace

Enrollment EnrollmentManager::enroll(const User& actor, std::string_view studentId, std::string_view courseCode,
                                     std::string_view term, std::optional<double> mark) {
    AccessPolicy::require(actor, Permission::ManageEnrollments, audit_);
    if (mark) AccessPolicy::require(actor, Permission::RecordGrades, audit_);
    const Key k = makeKey(studentId, courseCode, term);
    if (mark) {
        if (auto e = rules::checkMark(*mark)) throw ValidationException(*e);
        mark = round2(*mark);
    }
    Enrollment created;
    store_.transact([&](Database& db) {
        const Student* s = db.findStudent(k.student);
        if (s == nullptr) throw NotFoundException("Student " + k.student + " not found.");
        if (!s->active) throw ValidationException("Student " + k.student + " is deactivated.");
        const Course* c = db.findCourse(k.course);
        if (c == nullptr) throw NotFoundException("Course " + k.course + " not found.");
        if (!c->active) throw ValidationException("Course " + k.course + " is retired and cannot take new enrollments.");
        if (db.findEnrollment(k.student, k.course, k.term) != nullptr) {
            throw ValidationException("Student is already enrolled in " + k.course + " for " + k.term + ".");
        }
        created = Enrollment{k.student, k.course, k.term, mark, timeutil::nowTimestamp()};
        db.enrollments.push_back(created);
    });
    audit_.info("ENROLL", actor.username, k.student + " -> " + k.course + " (" + k.term + ")");
    if (mark) audit_.info("GRADE_RECORD", actor.username, k.student + " " + k.course + " (" + k.term + ")");
    return created;
}

void EnrollmentManager::withdraw(const User& actor, std::string_view studentId, std::string_view courseCode,
                                 std::string_view term) {
    AccessPolicy::require(actor, Permission::ManageEnrollments, audit_);
    const Key k = makeKey(studentId, courseCode, term);
    store_.transact([&](Database& db) {
        const auto it = std::find_if(db.enrollments.begin(), db.enrollments.end(), [&](const Enrollment& e) {
            return e.studentId == k.student && e.courseCode == k.course && e.term == k.term;
        });
        if (it == db.enrollments.end()) throw NotFoundException("Enrollment not found.");
        db.enrollments.erase(it);
    });
    audit_.warn("ENROLLMENT_REMOVED", actor.username, k.student + " x " + k.course + " (" + k.term + ")");
}

void EnrollmentManager::recordMark(const User& actor, std::string_view studentId, std::string_view courseCode,
                                   std::string_view term, double mark) {
    AccessPolicy::require(actor, Permission::RecordGrades, audit_);
    const Key k = makeKey(studentId, courseCode, term);
    if (auto e = rules::checkMark(mark)) throw ValidationException(*e);
    const double rounded = round2(mark);
    store_.transact([&](Database& db) {
        Enrollment* e = db.findEnrollment(k.student, k.course, k.term);
        if (e == nullptr) throw NotFoundException("Enrollment not found.");
        e->mark = rounded;
        e->recordedAt = timeutil::nowTimestamp();
    });
    audit_.info("GRADE_RECORD", actor.username, k.student + " " + k.course + " (" + k.term + ")");
}

void EnrollmentManager::clearMark(const User& actor, std::string_view studentId, std::string_view courseCode,
                                  std::string_view term) {
    AccessPolicy::require(actor, Permission::RecordGrades, audit_);
    const Key k = makeKey(studentId, courseCode, term);
    store_.transact([&](Database& db) {
        Enrollment* e = db.findEnrollment(k.student, k.course, k.term);
        if (e == nullptr) throw NotFoundException("Enrollment not found.");
        if (!e->mark) throw ValidationException("That enrollment has no grade to clear.");
        e->mark.reset();
        e->recordedAt = timeutil::nowTimestamp();
    });
    audit_.warn("GRADE_CLEARED", actor.username, k.student + " " + k.course + " (" + k.term + ")");
}

std::vector<Enrollment> EnrollmentManager::forStudent(const User& actor, std::string_view studentId) const {
    const std::string id = rules::normalizeStudentId(studentId);
    AccessPolicy::requireStudentAccess(actor, id, audit_);
    std::vector<Enrollment> out;
    for (const auto& e : store_.db().enrollments) {
        if (e.studentId == id) out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const Enrollment& a, const Enrollment& b) {
        return std::tie(a.term, a.courseCode) < std::tie(b.term, b.courseCode);
    });
    return out;
}

std::vector<Enrollment> EnrollmentManager::forCourse(const User& actor, std::string_view courseCode) const {
    AccessPolicy::require(actor, Permission::ViewAnyStudent, audit_);
    const std::string code = rules::normalizeCourseCode(courseCode);
    std::vector<Enrollment> out;
    for (const auto& e : store_.db().enrollments) {
        if (e.courseCode == code) out.push_back(e);
    }
    std::sort(out.begin(), out.end(), [](const Enrollment& a, const Enrollment& b) {
        return std::tie(a.term, a.studentId) < std::tie(b.term, b.studentId);
    });
    return out;
}

}  // namespace sms
