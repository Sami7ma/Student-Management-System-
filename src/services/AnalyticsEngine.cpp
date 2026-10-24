#include "sms/services/AnalyticsEngine.hpp"

#include <algorithm>
#include <cmath>
#include <map>

#include "sms/core/Exceptions.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Grading.hpp"
#include "sms/services/AccessPolicy.hpp"

namespace sms {

std::string_view toString(Standing s) {
    switch (s) {
        case Standing::DeansList: return "Dean's List";
        case Standing::GoodStanding: return "Good Standing";
        case Standing::Probation: return "Probation";
        case Standing::NoGrades: return "No grades";
    }
    return "";
}

double AnalyticsEngine::round2(double v) { return std::round(v * 100.0) / 100.0; }

namespace {

void accumulate(GpaResult& r, const Course& c, double mark) {
    r.qualityPoints += grading::pointsFromMark(mark) * c.credits;
    r.gradedCredits += c.credits;
    r.gradedEcts += c.ects;
    if (grading::isPassing(mark)) r.earnedCredits += c.credits;
    ++r.gradedCourses;
}

void finalize(GpaResult& r) {
    r.gpa = r.gradedCredits > 0 ? AnalyticsEngine::round2(r.qualityPoints / r.gradedCredits) : 0.0;
}

}  // namespace

GpaResult AnalyticsEngine::computeGpa(const Database& db, std::string_view studentId, std::string_view term) {
    GpaResult r;
    for (const auto& e : db.enrollments) {
        if (e.studentId != studentId || !e.mark) continue;
        if (!term.empty() && e.term != term) continue;
        const Course* c = db.findCourse(e.courseCode);
        if (c == nullptr) continue;
        accumulate(r, *c, *e.mark);
    }
    finalize(r);
    return r;
}

Standing AnalyticsEngine::classify(const GpaResult& g) {
    if (!g.hasGrades()) return Standing::NoGrades;
    if (g.gpa >= kDeansListGpa) return Standing::DeansList;
    if (g.gpa >= kGoodStandingGpa) return Standing::GoodStanding;
    return Standing::Probation;
}

StudentSummary AnalyticsEngine::summarize(const Database& db, const Student& student) {
    StudentSummary s;
    s.student = student;
    s.cumulative = computeGpa(db, student.id);
    s.standing = classify(s.cumulative);
    return s;
}

Transcript AnalyticsEngine::buildTranscript(const Database& db, const Student& student) {
    Transcript t;
    t.student = student;
    t.generatedAt = timeutil::nowTimestamp();

    std::map<std::string, std::vector<const Enrollment*>> byTerm;
    for (const auto& e : db.enrollments) {
        if (e.studentId == student.id) byTerm[e.term].push_back(&e);
    }
    GpaResult running;
    for (auto& [term, list] : byTerm) {
        std::sort(list.begin(), list.end(),
                  [](const Enrollment* a, const Enrollment* b) { return a->courseCode < b->courseCode; });
        TranscriptTerm block;
        block.term = term;
        for (const Enrollment* e : list) {
            const Course* c = db.findCourse(e->courseCode);
            if (c == nullptr) continue;
            TranscriptLine line;
            line.courseCode = c->code;
            line.title = c->title;
            line.credits = c->credits;
            line.ects = c->ects;
            line.mark = e->mark;
            if (e->mark) {
                line.letter = grading::letterFromMark(*e->mark);
                line.points = grading::pointsFromLetter(line.letter);
                accumulate(block.termGpa, *c, *e->mark);
                accumulate(running, *c, *e->mark);
            }
            block.lines.push_back(std::move(line));
        }
        finalize(block.termGpa);
        block.cumulativeAfter = running;
        finalize(block.cumulativeAfter);
        t.terms.push_back(std::move(block));
    }
    t.cumulative = running;
    finalize(t.cumulative);
    t.standing = classify(t.cumulative);
    return t;
}

CourseStats AnalyticsEngine::buildCourseStats(const Database& db, const Course& course) {
    CourseStats s;
    s.course = course;
    static const char* const kLetters[] = {"A", "A-", "B+", "B", "B-", "C+", "C", "C-", "D", "F"};
    for (const char* l : kLetters) s.distribution.emplace_back(l, 0);

    std::vector<double> marks;
    for (const auto& e : db.enrollments) {
        if (e.courseCode != course.code) continue;
        ++s.enrolled;
        if (!e.mark) continue;
        marks.push_back(*e.mark);
        if (grading::isPassing(*e.mark)) ++s.passed;
        const std::string letter = grading::letterFromMark(*e.mark);
        for (auto& [name, count] : s.distribution) {
            if (name == letter) ++count;
        }
    }
    s.graded = marks.size();
    if (marks.empty()) return s;

    std::sort(marks.begin(), marks.end());
    double sum = 0;
    for (double m : marks) sum += m;
    s.mean = sum / static_cast<double>(marks.size());
    const std::size_t n = marks.size();
    s.median = (n % 2 == 1) ? marks[n / 2] : (marks[n / 2 - 1] + marks[n / 2]) / 2.0;
    double sq = 0;
    for (double m : marks) sq += (m - s.mean) * (m - s.mean);
    s.stddev = std::sqrt(sq / static_cast<double>(n));  // population standard deviation
    s.minMark = marks.front();
    s.maxMark = marks.back();
    s.passRate = 100.0 * static_cast<double>(s.passed) / static_cast<double>(n);
    s.failRate = 100.0 - s.passRate;
    return s;
}

Transcript AnalyticsEngine::transcript(const User& actor, std::string_view studentId) const {
    AccessPolicy::requireStudentAccess(actor, studentId, audit_);
    const Student* s = store_.db().findStudent(studentId);
    if (s == nullptr) throw NotFoundException("Student " + std::string(studentId) + " not found.");
    audit_.info("TRANSCRIPT_VIEW", actor.username, "student=" + std::string(studentId));
    return buildTranscript(store_.db(), *s);
}

StudentSummary AnalyticsEngine::summary(const User& actor, std::string_view studentId) const {
    AccessPolicy::requireStudentAccess(actor, studentId, audit_);
    const Student* s = store_.db().findStudent(studentId);
    if (s == nullptr) throw NotFoundException("Student " + std::string(studentId) + " not found.");
    return summarize(store_.db(), *s);
}

std::vector<CourseStats> AnalyticsEngine::allCourseStats(const User& actor) const {
    AccessPolicy::require(actor, Permission::ViewAnalytics, audit_);
    std::vector<CourseStats> out;
    for (const auto& c : store_.db().courses) out.push_back(buildCourseStats(store_.db(), c));
    std::sort(out.begin(), out.end(), [](const CourseStats& a, const CourseStats& b) { return a.course.code < b.course.code; });
    return out;
}

CourseStats AnalyticsEngine::courseStats(const User& actor, std::string_view code) const {
    AccessPolicy::require(actor, Permission::ViewAnalytics, audit_);
    const Course* c = store_.db().findCourse(code);
    if (c == nullptr) throw NotFoundException("Course " + std::string(code) + " not found.");
    return buildCourseStats(store_.db(), *c);
}

std::vector<DepartmentStats> AnalyticsEngine::departmentStats(const User& actor) const {
    AccessPolicy::require(actor, Permission::ViewAnalytics, audit_);
    std::map<std::string, DepartmentStats> byDept;
    std::map<std::string, double> gpaSum;
    for (const auto& st : store_.db().students) {
        if (!st.active) continue;
        DepartmentStats& d = byDept[st.department];
        d.department = st.department;
        ++d.students;
        const StudentSummary sum = summarize(store_.db(), st);
        if (sum.cumulative.hasGrades()) {
            ++d.graded;
            gpaSum[st.department] += sum.cumulative.gpa;
        }
        switch (sum.standing) {
            case Standing::DeansList: ++d.deansList; break;
            case Standing::GoodStanding: ++d.goodStanding; break;
            case Standing::Probation: ++d.probation; break;
            case Standing::NoGrades: break;
        }
    }
    std::vector<DepartmentStats> out;
    for (auto& [name, d] : byDept) {
        d.averageGpa = d.graded > 0 ? round2(gpaSum[name] / static_cast<double>(d.graded)) : 0.0;
        out.push_back(d);
    }
    return out;
}

std::vector<StudentSummary> AnalyticsEngine::topStudents(const User& actor, std::size_t count) const {
    AccessPolicy::require(actor, Permission::ViewAnalytics, audit_);
    std::vector<StudentSummary> all;
    for (const auto& st : store_.db().students) {
        if (!st.active) continue;
        StudentSummary s = summarize(store_.db(), st);
        if (s.cumulative.hasGrades()) all.push_back(std::move(s));
    }
    std::sort(all.begin(), all.end(), [](const StudentSummary& a, const StudentSummary& b) {
        if (a.cumulative.gpa != b.cumulative.gpa) return a.cumulative.gpa > b.cumulative.gpa;
        return a.student.id < b.student.id;
    });
    if (all.size() > count) all.resize(count);
    return all;
}

Overview AnalyticsEngine::overview(const User& actor) const {
    AccessPolicy::require(actor, Permission::ViewAnalytics, audit_);
    const Database& db = store_.db();
    Overview o;
    double gpaSum = 0;
    std::size_t graded = 0;
    for (const auto& st : db.students) {
        if (!st.active) {
            ++o.inactiveStudents;
            continue;
        }
        ++o.activeStudents;
        const StudentSummary s = summarize(db, st);
        if (s.cumulative.hasGrades()) {
            gpaSum += s.cumulative.gpa;
            ++graded;
        }
        if (s.standing == Standing::DeansList) ++o.deansList;
        if (s.standing == Standing::GoodStanding) ++o.goodStanding;
        if (s.standing == Standing::Probation) ++o.probation;
    }
    for (const auto& c : db.courses) {
        if (c.active) ++o.activeCourses;
    }
    o.enrollments = db.enrollments.size();
    o.gradedEnrollments = static_cast<std::size_t>(
        std::count_if(db.enrollments.begin(), db.enrollments.end(), [](const Enrollment& e) { return e.mark.has_value(); }));
    o.averageGpa = graded > 0 ? round2(gpaSum / static_cast<double>(graded)) : 0.0;
    return o;
}

}  // namespace sms
