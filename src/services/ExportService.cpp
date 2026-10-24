#include "sms/services/ExportService.hpp"

#include <sstream>

#include "sms/core/Exceptions.hpp"
#include "sms/core/FileSystem.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Rules.hpp"
#include "sms/services/AccessPolicy.hpp"
#include "sms/services/TranscriptFormatter.hpp"

namespace sms {

namespace {

constexpr const char* kBom = "\xEF\xBB\xBF";

std::string num(double v) { return text::formatFixed(v, 2); }

std::string csvLine(const std::vector<std::string>& cells) {
    std::string line;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i > 0) line.push_back(',');
        line += text::csvEscape(cells[i]);
    }
    line += "\r\n";
    return line;
}

std::string mdRow(const std::vector<std::string>& cells) {
    std::string line = "|";
    for (const auto& c : cells) line += " " + text::markdownEscape(c) + " |";
    line += "\n";
    return line;
}

const std::vector<std::string> kStudentHeader = {"Student ID", "Name", "Email", "Phone", "Department",
                                                 "Date of Birth", "Enrolled On", "Status", "GPA",
                                                 "Graded Credits", "Standing"};
const std::vector<std::string> kCourseHeader = {"Code", "Title", "Department", "Credits", "ECTS", "Status",
                                                "Enrolled", "Graded", "Mean", "Median", "Std Dev", "Min", "Max",
                                                "Pass %", "Fail %"};

std::vector<std::string> studentCells(const StudentSummary& r) {
    const Student& s = r.student;
    return {s.id, s.name, s.email, s.phone, s.department, s.dateOfBirth.value_or(""), s.enrolledOn,
            s.active ? "Active" : "Deactivated",
            r.cumulative.hasGrades() ? num(r.cumulative.gpa) : "", num(r.cumulative.gradedCredits),
            std::string(toString(r.standing))};
}

std::vector<std::string> courseCells(const CourseStats& s) {
    const bool g = s.graded > 0;
    return {s.course.code, s.course.title, s.course.department, num(s.course.credits), num(s.course.ects),
            s.course.active ? "Active" : "Retired", std::to_string(s.enrolled), std::to_string(s.graded),
            g ? num(s.mean) : "", g ? num(s.median) : "", g ? num(s.stddev) : "", g ? num(s.minMark) : "",
            g ? num(s.maxMark) : "", g ? num(s.passRate) : "", g ? num(s.failRate) : ""};
}

}  // namespace

std::filesystem::path ExportService::write(const std::string& baseName, const std::string& extension,
                                           const std::string& content) {
    fsutil::ensureDirectory(directory_);
    const std::string stem = baseName + "_" + timeutil::fileStamp();
    std::filesystem::path path = directory_ / (stem + extension);
    for (int n = 2; std::filesystem::exists(path) && n < 1000; ++n) {
        path = directory_ / (stem + "_" + std::to_string(n) + extension);
    }
    fsutil::atomicWrite(path, content);
    return path;
}

std::filesystem::path ExportService::exportStudentsCsv(const User& actor, const std::vector<StudentSummary>& rows) {
    AccessPolicy::require(actor, Permission::ExportData, audit_);
    std::string out = kBom;
    out += csvLine(kStudentHeader);
    for (const auto& r : rows) out += csvLine(studentCells(r));
    const auto path = write("students", ".csv", out);
    audit_.info("EXPORT", actor.username, "students csv rows=" + std::to_string(rows.size()) + " file=" + path.filename().string());
    return path;
}

std::filesystem::path ExportService::exportStudentsMarkdown(const User& actor, const std::vector<StudentSummary>& rows) {
    AccessPolicy::require(actor, Permission::ExportData, audit_);
    std::string out = "# Student Report\n\nGenerated: " + timeutil::nowTimestamp() + " by " +
                      text::markdownEscape(actor.username) + "  \nRecords: " + std::to_string(rows.size()) + "\n\n";
    out += mdRow(kStudentHeader);
    out += "|" + text::repeat(" --- |", kStudentHeader.size()) + "\n";
    for (const auto& r : rows) out += mdRow(studentCells(r));
    const auto path = write("students", ".md", out);
    audit_.info("EXPORT", actor.username, "students markdown rows=" + std::to_string(rows.size()) + " file=" + path.filename().string());
    return path;
}

std::filesystem::path ExportService::exportCourseStatsCsv(const User& actor) {
    AccessPolicy::require(actor, Permission::ExportData, audit_);
    std::string out = kBom;
    out += csvLine(kCourseHeader);
    std::size_t n = 0;
    for (const auto& c : store_.db().courses) {
        out += csvLine(courseCells(AnalyticsEngine::buildCourseStats(store_.db(), c)));
        ++n;
    }
    const auto path = write("courses", ".csv", out);
    audit_.info("EXPORT", actor.username, "courses csv rows=" + std::to_string(n) + " file=" + path.filename().string());
    return path;
}

std::filesystem::path ExportService::exportCourseStatsMarkdown(const User& actor) {
    AccessPolicy::require(actor, Permission::ExportData, audit_);
    std::string out = "# Course Statistics Report\n\nGenerated: " + timeutil::nowTimestamp() + "\n\n";
    out += mdRow(kCourseHeader);
    out += "|" + text::repeat(" --- |", kCourseHeader.size()) + "\n";
    std::size_t n = 0;
    for (const auto& c : store_.db().courses) {
        out += mdRow(courseCells(AnalyticsEngine::buildCourseStats(store_.db(), c)));
        ++n;
    }
    const auto path = write("courses", ".md", out);
    audit_.info("EXPORT", actor.username, "courses markdown rows=" + std::to_string(n) + " file=" + path.filename().string());
    return path;
}

std::filesystem::path ExportService::exportTranscript(const User& actor, std::string_view studentIdRaw) {
    const std::string id = rules::normalizeStudentId(studentIdRaw);
    if (actor.role == Role::Student) {
        AccessPolicy::require(actor, Permission::ExportOwnTranscript, audit_);
        AccessPolicy::requireStudentAccess(actor, id, audit_);
    } else {
        AccessPolicy::require(actor, Permission::ExportData, audit_);
    }
    const Student* s = store_.db().findStudent(id);
    if (s == nullptr) throw NotFoundException("Student " + id + " not found.");
    std::string safeId = id;
    for (char& c : safeId) {
        if (c == '/') c = '-';
    }
    const Transcript t = AnalyticsEngine::buildTranscript(store_.db(), *s);
    const auto path = write("transcript_" + safeId, ".txt", std::string(kBom) + formatTranscriptText(t));
    audit_.info("EXPORT", actor.username, "transcript student=" + id + " file=" + path.filename().string());
    return path;
}

}  // namespace sms
