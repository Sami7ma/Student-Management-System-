#include "sms/services/TranscriptFormatter.hpp"

#include <sstream>

#include "sms/core/Text.hpp"

namespace sms {

namespace {
constexpr std::size_t kWidth = 78;

std::string num(double v) { return text::formatFixed(v, 2); }

void field(std::ostringstream& os, const std::string& label, const std::string& value) {
    os << text::padRight(label, 12) << ": " << value << "\n";
}
}  // namespace

std::string formatTranscriptText(const Transcript& t) {
    std::ostringstream os;
    const std::string heavy(kWidth, '=');
    const std::string light(kWidth, '-');
    os << heavy << "\n" << text::padCenter("OFFICIAL ACADEMIC TRANSCRIPT", kWidth) << "\n" << heavy << "\n";
    field(os, "Student ID", t.student.id);
    field(os, "Name", t.student.name);
    field(os, "Department", t.student.department);
    if (t.student.dateOfBirth) field(os, "Born", *t.student.dateOfBirth);
    field(os, "Enrolled", t.student.enrolledOn);
    field(os, "Generated", t.generatedAt);
    os << light << "\n";

    if (t.terms.empty()) os << "No enrollments recorded.\n";
    for (const auto& term : t.terms) {
        os << "Term: " << term.term << "\n";
        os << "  " << text::padRight("Code", 10) << text::padRight("Course title", 34) << text::padLeft("Cr", 5)
           << text::padLeft("ECTS", 6) << text::padLeft("Mark", 8) << "  " << text::padRight("Gr", 4)
           << text::padLeft("Pts", 5) << "\n";
        os << "  " << std::string(10 - 1, '-') << " " << std::string(33, '-') << " " << std::string(4, '-') << " "
           << std::string(5, '-') << " " << std::string(7, '-') << "  " << std::string(3, '-') << " "
           << std::string(4, '-') << "\n";
        for (const auto& l : term.lines) {
            os << "  " << text::padRight(l.courseCode, 10) << text::padRight(text::truncate(l.title, 33, "~"), 34)
               << text::padLeft(num(l.credits), 5) << text::padLeft(num(l.ects), 6)
               << text::padLeft(l.mark ? num(*l.mark) : "-", 8) << "  "
               << text::padRight(l.letter.empty() ? "-" : l.letter, 4)
               << text::padLeft(l.mark ? num(l.points) : "-", 5) << "\n";
        }
        os << "  Term GPA: " << (term.termGpa.hasGrades() ? num(term.termGpa.gpa) : "n/a")
           << "   Cumulative GPA: " << (term.cumulativeAfter.hasGrades() ? num(term.cumulativeAfter.gpa) : "n/a")
           << "\n\n";
    }
    os << light << "\n";
    os << "Graded courses     : " << t.cumulative.gradedCourses << "\n";
    os << "Credits attempted  : " << num(t.cumulative.gradedCredits) << "\n";
    os << "Credits earned     : " << num(t.cumulative.earnedCredits) << "\n";
    os << "ECTS (graded)      : " << num(t.cumulative.gradedEcts) << "\n";
    os << "Cumulative GPA     : " << (t.cumulative.hasGrades() ? num(t.cumulative.gpa) : "n/a") << " / 4.00\n";
    os << "Academic standing  : " << toString(t.standing) << "\n";
    os << heavy << "\n";
    os << text::padCenter("*** END OF TRANSCRIPT ***", kWidth) << "\n";
    return os.str();
}

}  // namespace sms
