#include "sms/services/LegacyImporter.hpp"

#include <cmath>

#include "sms/core/Exceptions.hpp"
#include "sms/core/FileSystem.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Grading.hpp"
#include "sms/models/Rules.hpp"
#include "sms/services/AccessPolicy.hpp"

namespace sms {

namespace {

struct LegacyCourse {
    std::string title, code, grade;
    double credit = 0, ects = 0;
};
struct LegacyStudent {
    std::string id, name, major, phone;
    std::vector<LegacyCourse> courses;
};

std::vector<std::string> readLines(const std::string& content) {
    std::vector<std::string> lines = text::split(content, '\n');
    for (auto& l : lines) {
        if (!l.empty() && l.back() == '\r') l.pop_back();
    }
    return lines;
}

}  // namespace

ImportReport LegacyImporter::import(const User& actor, const std::filesystem::path& file) {
    AccessPolicy::require(actor, Permission::ImportLegacy, audit_);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) throw NotFoundException("File not found: " + file.string());
    const std::vector<std::string> lines = readLines(fsutil::readAll(file));

    ImportReport report;
    std::vector<LegacyStudent> parsed;

    // ---- Parse (tolerant: a damaged block is skipped, not fatal) ----
    std::size_t i = 0;
    while (i < lines.size()) {
        if (lines[i] != "STUDENT") {
            ++i;
            continue;
        }
        const std::size_t blockStart = i + 1;
        std::size_t end = blockStart;
        while (end < lines.size() && lines[end] != "ENDSTUDENT") ++end;
        if (end >= lines.size()) {
            report.warnings.push_back("Line " + std::to_string(i + 1) + ": student block has no ENDSTUDENT; skipped.");
            ++report.studentsSkipped;
            break;
        }
        bool ok = (end - blockStart) >= 4;
        LegacyStudent s;
        if (ok) {
            s.id = lines[blockStart];
            s.name = lines[blockStart + 1];
            s.major = lines[blockStart + 2];
            s.phone = lines[blockStart + 3];
            std::size_t k = blockStart + 4;
            while (k < end) {
                // A course block is the marker line plus five value lines, all before ENDSTUDENT.
                if (lines[k] != "COURSE" || k + 5 >= end) {
                    ok = false;
                    break;
                }
                LegacyCourse c;
                c.title = lines[k + 1];
                c.code = lines[k + 2];
                const auto credit = text::parseDouble(lines[k + 3]);
                const auto ects = text::parseDouble(lines[k + 4]);
                c.grade = text::trim(lines[k + 5]);
                if (!credit || !ects) {
                    report.warnings.push_back("Student " + s.id + ": course '" + text::sanitizeForLog(c.code, 20) +
                                              "' has non-numeric credit/ECTS; course skipped.");
                } else {
                    c.credit = *credit;
                    c.ects = *ects;
                    s.courses.push_back(std::move(c));
                }
                k += 6;
            }
        }
        if (!ok) {
            report.warnings.push_back("Line " + std::to_string(i + 1) + ": malformed student block; skipped.");
            ++report.studentsSkipped;
        } else {
            parsed.push_back(std::move(s));
        }
        i = end + 1;
    }

    backups_.snapshot(actor.username, "pre-import");

    // ---- Apply atomically ----
    const std::string today = timeutil::today();
    const std::string now = timeutil::nowTimestamp();
    store_.transact([&](Database& db) {
        for (auto& ls : parsed) {
            Student st;
            st.id = rules::normalizeStudentId(ls.id);
            st.name = rules::collapseSpaces(ls.name);
            st.department = rules::collapseSpaces(ls.major);
            st.phone = rules::normalizePhone(ls.phone);
            st.enrolledOn = today;
            if (!st.phone.empty() && rules::checkPhone(st.phone)) {
                report.warnings.push_back("Student " + st.id + ": invalid phone dropped.");
                st.phone.clear();
            }
            if (auto e = rules::validateStudent(st, today)) {
                report.warnings.push_back("Student '" + text::sanitizeForLog(ls.id, 30) + "' skipped: " + *e);
                ++report.studentsSkipped;
                continue;
            }
            if (db.findStudent(st.id) != nullptr) {
                report.warnings.push_back("Student " + st.id + " already exists; skipped.");
                ++report.studentsSkipped;
                continue;
            }
            db.students.push_back(st);
            ++report.studentsImported;

            for (const auto& lc : ls.courses) {
                Course course;
                course.code = rules::normalizeCourseCode(lc.code);
                course.title = rules::collapseSpaces(lc.title);
                course.credits = lc.credit;
                course.ects = lc.ects;
                course.department = st.department;
                if (auto e = rules::validateCourse(course)) {
                    report.warnings.push_back("Student " + st.id + ": course '" + text::sanitizeForLog(lc.code, 20) +
                                              "' skipped: " + *e);
                    continue;
                }
                if (const Course* existing = db.findCourse(course.code)) {
                    if (std::fabs(existing->credits - course.credits) > 0.001) {
                        report.warnings.push_back("Course " + course.code + ": credit differs from the catalogue; kept the catalogue value.");
                    }
                } else {
                    db.courses.push_back(course);
                    ++report.coursesCreated;
                }
                if (db.findEnrollment(st.id, course.code, "LEGACY") != nullptr) {
                    report.warnings.push_back("Student " + st.id + ": duplicate course " + course.code + " ignored.");
                    continue;
                }
                bool known = false;
                const double mark = grading::representativeMark(lc.grade, known);
                if (!known) {
                    report.warnings.push_back("Student " + st.id + ": unknown grade '" + text::sanitizeForLog(lc.grade, 10) +
                                              "' for " + course.code + "; imported ungraded.");
                }
                db.enrollments.push_back(
                    Enrollment{st.id, course.code, "LEGACY", known ? std::optional<double>(mark) : std::nullopt, now});
                ++report.enrollmentsImported;
            }
        }
    });
    audit_.critical("LEGACY_IMPORT", actor.username,
                    "students=" + std::to_string(report.studentsImported) +
                        " skipped=" + std::to_string(report.studentsSkipped) +
                        " enrollments=" + std::to_string(report.enrollmentsImported));
    return report;
}

}  // namespace sms
