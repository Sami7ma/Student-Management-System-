#include "sms/services/CourseManager.hpp"

#include <algorithm>
#include <cmath>

#include "sms/core/Exceptions.hpp"
#include "sms/models/Rules.hpp"
#include "sms/services/AccessPolicy.hpp"

namespace sms {

namespace {
void normalize(Course& c) {
    c.code = rules::normalizeCourseCode(c.code);
    c.title = rules::collapseSpaces(c.title);
    c.department = rules::collapseSpaces(c.department);
    c.credits = std::round(c.credits * 100.0) / 100.0;
    c.ects = std::round(c.ects * 100.0) / 100.0;
}
void validate(const Course& c) {
    if (auto e = rules::validateCourse(c)) throw ValidationException(*e);
}
}  // namespace

Course CourseManager::create(const User& actor, Course draft) {
    AccessPolicy::require(actor, Permission::ManageCourses, audit_);
    normalize(draft);
    draft.active = true;
    validate(draft);
    store_.transact([&](Database& db) {
        if (db.findCourse(draft.code) != nullptr) {
            throw ValidationException("Course " + draft.code + " already exists.");
        }
        db.courses.push_back(draft);
    });
    audit_.info("COURSE_CREATE", actor.username, "code=" + draft.code);
    return draft;
}

Course CourseManager::update(const User& actor, const Course& input) {
    AccessPolicy::require(actor, Permission::ManageCourses, audit_);
    Course draft = input;
    normalize(draft);
    validate(draft);
    store_.transact([&](Database& db) {
        Course* c = db.findCourse(draft.code);
        if (c == nullptr) throw NotFoundException("Course " + draft.code + " not found.");
        draft.active = c->active;
        *c = draft;
    });
    audit_.info("COURSE_UPDATE", actor.username, "code=" + draft.code);
    return draft;
}

void CourseManager::setActive(const User& actor, std::string_view codeRaw, bool active) {
    AccessPolicy::require(actor, Permission::ManageCourses, audit_);
    const std::string code = rules::normalizeCourseCode(codeRaw);
    store_.transact([&](Database& db) {
        Course* c = db.findCourse(code);
        if (c == nullptr) throw NotFoundException("Course " + code + " not found.");
        if (c->active == active) {
            throw ValidationException("Course " + code + (active ? " is already active." : " is already retired."));
        }
        c->active = active;
    });
    audit_.warn(active ? "COURSE_REACTIVATE" : "COURSE_SOFT_DELETE", actor.username, "code=" + code);
}

std::size_t CourseManager::hardDelete(const User& actor, std::string_view codeRaw) {
    AccessPolicy::require(actor, Permission::HardDeleteCourse, audit_);
    const std::string code = rules::normalizeCourseCode(codeRaw);
    std::size_t removed = 0;
    store_.transact([&](Database& db) {
        const auto it = std::find_if(db.courses.begin(), db.courses.end(), [&](const Course& c) { return c.code == code; });
        if (it == db.courses.end()) throw NotFoundException("Course " + code + " not found.");
        db.courses.erase(it);
        const auto cut = std::remove_if(db.enrollments.begin(), db.enrollments.end(),
                                        [&](const Enrollment& e) { return e.courseCode == code; });
        removed = static_cast<std::size_t>(db.enrollments.end() - cut);
        db.enrollments.erase(cut, db.enrollments.end());
    });
    audit_.critical("COURSE_HARD_DELETE", actor.username, "code=" + code + " enrollments=" + std::to_string(removed));
    return removed;
}

std::optional<Course> CourseManager::find(const User& actor, std::string_view codeRaw) const {
    if (!actor.active) throw AuthException("This account is disabled.");
    const Course* c = store_.db().findCourse(rules::normalizeCourseCode(codeRaw));
    if (c == nullptr) return std::nullopt;
    return *c;
}

std::vector<Course> CourseManager::list(const User& actor, bool includeInactive) const {
    AccessPolicy::require(actor, Permission::ViewAnyStudent, audit_);
    std::vector<Course> out;
    for (const auto& c : store_.db().courses) {
        if (c.active || includeInactive) out.push_back(c);
    }
    std::sort(out.begin(), out.end(), [](const Course& a, const Course& b) { return a.code < b.code; });
    return out;
}

}  // namespace sms
