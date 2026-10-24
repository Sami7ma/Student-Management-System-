#include "sms/services/StudentManager.hpp"

#include <algorithm>
#include <set>

#include "sms/core/Exceptions.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Rules.hpp"
#include "sms/services/AccessPolicy.hpp"

namespace sms {

namespace {

void normalize(Student& s) {
    s.id = rules::normalizeStudentId(s.id);
    s.name = rules::collapseSpaces(s.name);
    s.email = text::trim(s.email);
    s.phone = rules::normalizePhone(s.phone);
    s.department = rules::collapseSpaces(s.department);
    if (s.dateOfBirth && text::trim(*s.dateOfBirth).empty()) s.dateOfBirth.reset();
    if (s.enrolledOn.empty()) s.enrolledOn = timeutil::today();
}

void validate(const Student& s) {
    if (auto e = rules::validateStudent(s, timeutil::today())) throw ValidationException(*e);
}

std::string changedFields(const Student& a, const Student& b) {
    std::string out;
    auto add = [&](bool changed, const char* name) {
        if (!changed) return;
        if (!out.empty()) out += ",";
        out += name;
    };
    add(a.name != b.name, "name");
    add(a.email != b.email, "email");
    add(a.phone != b.phone, "phone");
    add(a.department != b.department, "department");
    add(a.dateOfBirth != b.dateOfBirth, "dateOfBirth");
    add(a.enrolledOn != b.enrolledOn, "enrolledOn");
    return out.empty() ? "no changes" : out;
}

}  // namespace

Student StudentManager::create(const User& actor, Student draft) {
    AccessPolicy::require(actor, Permission::CreateStudent, audit_);
    normalize(draft);
    draft.active = true;
    draft.deletedAt.clear();
    validate(draft);
    store_.transact([&](Database& db) {
        if (db.findStudent(draft.id) != nullptr) {
            throw ValidationException("A student with ID " + draft.id + " already exists.");
        }
        db.students.push_back(draft);
    });
    audit_.info("STUDENT_CREATE", actor.username, "id=" + draft.id);
    return draft;
}

Student StudentManager::update(const User& actor, const Student& input) {
    AccessPolicy::require(actor, Permission::UpdateStudent, audit_);
    Student draft = input;
    normalize(draft);
    validate(draft);
    std::string changes;
    store_.transact([&](Database& db) {
        Student* s = db.findStudent(draft.id);
        if (s == nullptr) throw NotFoundException("Student " + draft.id + " not found.");
        if (!s->active) throw ValidationException("Student " + draft.id + " is deactivated; restore it first.");
        draft.active = s->active;
        draft.deletedAt = s->deletedAt;
        changes = changedFields(*s, draft);
        *s = draft;
    });
    audit_.info("STUDENT_UPDATE", actor.username, "id=" + draft.id + " fields=" + changes);
    return draft;
}

void StudentManager::softDelete(const User& actor, std::string_view idRaw) {
    AccessPolicy::require(actor, Permission::SoftDeleteStudent, audit_);
    const std::string id = rules::normalizeStudentId(idRaw);
    store_.transact([&](Database& db) {
        Student* s = db.findStudent(id);
        if (s == nullptr) throw NotFoundException("Student " + id + " not found.");
        if (!s->active) throw ValidationException("Student " + id + " is already deactivated.");
        s->active = false;
        s->deletedAt = timeutil::nowTimestamp();
        for (auto& u : db.users) {
            if (u.linkedStudentId == id) u.active = false;  // a deactivated student cannot log in
        }
    });
    audit_.critical("STUDENT_SOFT_DELETE", actor.username, "id=" + id);
}

void StudentManager::restore(const User& actor, std::string_view idRaw) {
    AccessPolicy::require(actor, Permission::RestoreStudent, audit_);
    const std::string id = rules::normalizeStudentId(idRaw);
    store_.transact([&](Database& db) {
        Student* s = db.findStudent(id);
        if (s == nullptr) throw NotFoundException("Student " + id + " not found.");
        if (s->active) throw ValidationException("Student " + id + " is not deactivated.");
        s->active = true;
        s->deletedAt.clear();
    });
    audit_.critical("STUDENT_RESTORE", actor.username, "id=" + id);
}

void StudentManager::hardDelete(const User& actor, std::string_view idRaw) {
    AccessPolicy::require(actor, Permission::HardDeleteStudent, audit_);
    const std::string id = rules::normalizeStudentId(idRaw);
    std::size_t removedEnrollments = 0;
    std::size_t removedUsers = 0;
    store_.transact([&](Database& db) {
        const auto it = std::find_if(db.students.begin(), db.students.end(), [&](const Student& s) { return s.id == id; });
        if (it == db.students.end()) throw NotFoundException("Student " + id + " not found.");
        db.students.erase(it);
        const auto eraseEnroll = std::remove_if(db.enrollments.begin(), db.enrollments.end(),
                                                [&](const Enrollment& e) { return e.studentId == id; });
        removedEnrollments = static_cast<std::size_t>(db.enrollments.end() - eraseEnroll);
        db.enrollments.erase(eraseEnroll, db.enrollments.end());
        const auto eraseUsers = std::remove_if(db.users.begin(), db.users.end(),
                                               [&](const User& u) { return u.linkedStudentId == id; });
        removedUsers = static_cast<std::size_t>(db.users.end() - eraseUsers);
        db.users.erase(eraseUsers, db.users.end());
    });
    audit_.critical("STUDENT_HARD_DELETE", actor.username,
                    "id=" + id + " enrollments=" + std::to_string(removedEnrollments) +
                        " accounts=" + std::to_string(removedUsers));
}

std::optional<Student> StudentManager::find(const User& actor, std::string_view idRaw) const {
    const std::string id = rules::normalizeStudentId(idRaw);
    AccessPolicy::requireStudentAccess(actor, id, audit_);
    const Student* s = store_.db().findStudent(id);
    if (s == nullptr) return std::nullopt;
    audit_.info("STUDENT_VIEW", actor.username, "id=" + id);
    return *s;
}

std::vector<StudentSummary> StudentManager::search(const User& actor, const SearchCriteria& c,
                                                   const SortSpec& sort) const {
    AccessPolicy::require(actor, Permission::ViewAnyStudent, audit_);
    if (c.gpaMin || c.gpaMax) {
        if (auto e = rules::checkGpaRange(c.gpaMin.value_or(rules::kMinGpa), c.gpaMax.value_or(rules::kMaxGpa))) {
            throw ValidationException(*e);
        }
    }
    std::vector<StudentSummary> rows;
    for (const auto& s : store_.db().students) {
        if (c.onlyInactive ? s.active : (!s.active && !c.includeInactive)) continue;
        if (!c.nameContains.empty() && !text::icontains(s.name, c.nameContains)) continue;
        if (!c.idContains.empty() && !text::icontains(s.id, c.idContains)) continue;
        if (!c.department.empty() && !text::icontains(s.department, c.department)) continue;
        StudentSummary sum = AnalyticsEngine::summarize(store_.db(), s);
        if (c.gpaMin || c.gpaMax) {
            if (!sum.cumulative.hasGrades()) continue;
            if (c.gpaMin && sum.cumulative.gpa < *c.gpaMin) continue;
            if (c.gpaMax && sum.cumulative.gpa > *c.gpaMax) continue;
        }
        rows.push_back(std::move(sum));
    }

    auto cmp = [&](const StudentSummary& a, const StudentSummary& b) {
        int r = 0;  // <0: a before b
        switch (sort.key) {
            case SortKey::Name: r = text::toLower(a.student.name).compare(text::toLower(b.student.name)); break;
            case SortKey::Id: r = a.student.id.compare(b.student.id); break;
            case SortKey::Department:
                r = text::toLower(a.student.department).compare(text::toLower(b.student.department));
                break;
            case SortKey::EnrolledOn: r = a.student.enrolledOn.compare(b.student.enrolledOn); break;
            case SortKey::Gpa:
                r = (a.cumulative.gpa < b.cumulative.gpa) ? -1 : (a.cumulative.gpa > b.cumulative.gpa ? 1 : 0);
                break;
        }
        if (r == 0) return a.student.id < b.student.id;  // deterministic tie-break
        return sort.ascending ? r < 0 : r > 0;
    };
    std::stable_sort(rows.begin(), rows.end(), cmp);
    return rows;
}

std::vector<std::string> StudentManager::departments(const User& actor) const {
    AccessPolicy::require(actor, Permission::ViewAnyStudent, audit_);
    std::set<std::string> names;
    for (const auto& s : store_.db().students) {
        if (s.active) names.insert(s.department);
    }
    return {names.begin(), names.end()};
}

}  // namespace sms
