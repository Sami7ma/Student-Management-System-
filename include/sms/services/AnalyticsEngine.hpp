#pragma once
// Academic analytics: GPA / CGPA, standing classification, course statistics, transcripts.
//
// Policy constants (documented in README):
//   * GPA is the credit-weighted mean of grade points over graded enrollments; every graded
//     attempt counts (retakes are separate rows). Rounded to 2 decimals.
//   * Standing: Dean's List >= 3.50, Good Standing >= 2.00, Probation < 2.00 ("No grades" if none).
//   * A mark >= 40 (letter D or better) is a pass.
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "sms/models/Database.hpp"
#include "sms/storage/DataStore.hpp"
#include "sms/storage/Logger.hpp"

namespace sms {

constexpr double kDeansListGpa = 3.50;
constexpr double kGoodStandingGpa = 2.00;

enum class Standing { DeansList, GoodStanding, Probation, NoGrades };
std::string_view toString(Standing s);

struct GpaResult {
    double qualityPoints = 0;
    double gradedCredits = 0;
    double gradedEcts = 0;
    double earnedCredits = 0;  // credits of passed courses
    double gpa = 0;            // rounded to 2 decimals
    std::size_t gradedCourses = 0;
    bool hasGrades() const { return gradedCourses > 0; }
};

struct StudentSummary {
    Student student;
    GpaResult cumulative;
    Standing standing = Standing::NoGrades;
};

struct TranscriptLine {
    std::string courseCode;
    std::string title;
    double credits = 0;
    double ects = 0;
    std::optional<double> mark;
    std::string letter;  // empty when ungraded
    double points = 0;
};

struct TranscriptTerm {
    std::string term;
    std::vector<TranscriptLine> lines;
    GpaResult termGpa;
    GpaResult cumulativeAfter;
};

struct Transcript {
    Student student;
    std::vector<TranscriptTerm> terms;
    GpaResult cumulative;
    Standing standing = Standing::NoGrades;
    std::string generatedAt;
};

struct CourseStats {
    Course course;
    std::size_t enrolled = 0;
    std::size_t graded = 0;
    std::size_t passed = 0;
    double mean = 0, median = 0, stddev = 0, minMark = 0, maxMark = 0;
    double passRate = 0, failRate = 0;  // percentages of graded
    std::vector<std::pair<std::string, std::size_t>> distribution;  // letter -> count
};

struct DepartmentStats {
    std::string department;
    std::size_t students = 0;
    std::size_t graded = 0;
    double averageGpa = 0;
    std::size_t deansList = 0, goodStanding = 0, probation = 0;
};

struct Overview {
    std::size_t activeStudents = 0, inactiveStudents = 0, activeCourses = 0, enrollments = 0, gradedEnrollments = 0;
    double averageGpa = 0;
    std::size_t deansList = 0, goodStanding = 0, probation = 0;
};

class AnalyticsEngine {
public:
    AnalyticsEngine(storage::DataStore& store, storage::AuditLogger& audit) : store_(store), audit_(audit) {}

    // ---- Pure computations (no access control; used by other services) ----
    static double round2(double v);
    static GpaResult computeGpa(const Database& db, std::string_view studentId, std::string_view term = {});
    static Standing classify(const GpaResult& gpa);
    static StudentSummary summarize(const Database& db, const Student& student);
    static Transcript buildTranscript(const Database& db, const Student& student);
    static CourseStats buildCourseStats(const Database& db, const Course& course);

    // ---- Access-controlled queries ----
    Transcript transcript(const User& actor, std::string_view studentId) const;
    StudentSummary summary(const User& actor, std::string_view studentId) const;
    std::vector<CourseStats> allCourseStats(const User& actor) const;
    CourseStats courseStats(const User& actor, std::string_view courseCode) const;
    std::vector<DepartmentStats> departmentStats(const User& actor) const;
    std::vector<StudentSummary> topStudents(const User& actor, std::size_t count) const;
    Overview overview(const User& actor) const;

private:
    storage::DataStore& store_;
    storage::AuditLogger& audit_;
};

}  // namespace sms
