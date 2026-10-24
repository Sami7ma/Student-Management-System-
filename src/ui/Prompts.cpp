#include "sms/ui/Prompts.hpp"

#include "sms/core/Exceptions.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Grading.hpp"
#include "sms/models/Rules.hpp"

namespace sms::ui {

std::string askStudentId(AppContext& ctx, const std::string& label) {
    const std::string raw = ctx.in.line(label + " (e.g. NSE/1001/16)", 20, false, [](const std::string& s) {
        return rules::checkStudentId(rules::normalizeStudentId(s));
    });
    return rules::normalizeStudentId(raw);
}

std::string askCourseCode(AppContext& ctx, const std::string& label) {
    const std::string raw = ctx.in.line(label + " (e.g. COSC1011)", 14, false, [](const std::string& s) {
        return rules::checkCourseCode(rules::normalizeCourseCode(s));
    });
    return rules::normalizeCourseCode(raw);
}

std::string defaultTerm() {
    const std::string today = timeutil::today();
    const int month = (today[5] - '0') * 10 + (today[6] - '0');
    return today.substr(0, 4) + (month >= 2 && month <= 7 ? "-S2" : "-S1");
}

std::string askTerm(AppContext& ctx) {
    const std::string term = ctx.in.line("Term", 16, false, [](const std::string& s) { return rules::checkTerm(s); },
                                         ctx.lastTerm.empty() ? defaultTerm() : ctx.lastTerm);
    ctx.lastTerm = term;
    return term;
}

void renderProfile(AppContext& ctx, const std::string& studentId) {
    const auto student = ctx.students.find(ctx.user(), studentId);
    if (!student) throw NotFoundException("Student " + studentId + " not found.");
    const Student& s = *student;
    const StudentSummary sum = ctx.analytics.summary(ctx.user(), s.id);

    std::string dob = "-";
    if (s.dateOfBirth) dob = *s.dateOfBirth + " (age " + std::to_string(timeutil::ageOn(*s.dateOfBirth, timeutil::today())) + ")";
    ctx.ui.keyValues("Student Profile",
                     {{"Student ID", s.id},
                      {"Name", s.name},
                      {"Email", s.email.empty() ? "-" : s.email},
                      {"Phone", s.phone.empty() ? "-" : s.phone},
                      {"Department", s.department},
                      {"Date of birth", dob},
                      {"Enrolled on", s.enrolledOn},
                      {"Status", s.active ? "Active" : "Deactivated on " + s.deletedAt},
                      {"Cumulative GPA", sum.cumulative.hasGrades() ? fmtNum(sum.cumulative.gpa) + " / 4.00" : "n/a"},
                      {"Credits (graded / earned)", fmtNum(sum.cumulative.gradedCredits, 1) + " / " + fmtNum(sum.cumulative.earnedCredits, 1)}});
    ctx.ui.out() << " Academic standing: " << ctx.term.paint(std::string(toString(sum.standing)), standingStyle(sum.standing)) << "\n";
    const Transcript t = AnalyticsEngine::buildTranscript(ctx.store.db(), s);

    TableBuilder table = ctx.ui.table();
    table.title("Enrollments and grades")
        .column({"Term", Align::Left, 4, 0})
        .column({"Code", Align::Left, 6, 0})
        .column({"Course", Align::Left, 8, 0})
        .column({"Cr", Align::Right, 2, 0})
        .column({"Mark", Align::Right, 4, 0})
        .column({"Grade", Align::Center, 5, 0})
        .column({"Pts", Align::Right, 3, 0});
    for (const auto& term : t.terms) {
        for (const auto& l : term.lines) {
            const Style gs = l.letter.empty() ? Style::Gray : (l.mark && *l.mark < grading::kPassMark ? Style::Red : Style::Green);
            table.row(std::vector<Cell>{{term.term, {}}, {l.courseCode, {}}, {l.title, {}}, {fmtNum(l.credits, 1), {}},
                                        {l.mark ? fmtNum(*l.mark) : "-", {}},
                                        {l.letter.empty() ? "-" : l.letter, ctx.term.code(gs)},
                                        {l.mark ? fmtNum(l.points) : "-", {}}});
        }
    }
    ctx.ui.out() << table.render();
}

void renderTranscript(AppContext& ctx, const Transcript& t) {
    ctx.ui.keyValues("Academic Transcript",
                     {{"Student ID", t.student.id},
                      {"Name", t.student.name},
                      {"Department", t.student.department},
                      {"Generated", t.generatedAt}});
    if (t.terms.empty()) ctx.ui.info("No enrollments recorded for this student yet.");
    for (const auto& term : t.terms) {
        TableBuilder table = ctx.ui.table();
        table.title("Term " + term.term)
            .column({"Code", Align::Left, 6, 0})
            .column({"Course", Align::Left, 8, 0})
            .column({"Cr", Align::Right, 2, 0})
            .column({"ECTS", Align::Right, 4, 0})
            .column({"Mark", Align::Right, 4, 0})
            .column({"Grade", Align::Center, 5, 0})
            .column({"Pts", Align::Right, 3, 0});
        for (const auto& l : term.lines) {
            const Style gs = l.letter.empty() ? Style::Gray : (l.mark && *l.mark < grading::kPassMark ? Style::Red : Style::Green);
            table.row(std::vector<Cell>{{l.courseCode, {}}, {l.title, {}}, {fmtNum(l.credits, 1), {}}, {fmtNum(l.ects, 1), {}},
                                        {l.mark ? fmtNum(*l.mark) : "-", {}},
                                        {l.letter.empty() ? "-" : l.letter, ctx.term.code(gs)},
                                        {l.mark ? fmtNum(l.points) : "-", {}}});
        }
        table.footer("Term GPA " + (term.termGpa.hasGrades() ? fmtNum(term.termGpa.gpa) : std::string("n/a")) + "   |   Cumulative GPA " +
                     (term.cumulativeAfter.hasGrades() ? fmtNum(term.cumulativeAfter.gpa) : std::string("n/a")));
        ctx.ui.out() << table.render();
    }
    ctx.ui.out() << "\n Cumulative GPA: "
                 << ctx.term.paint(t.cumulative.hasGrades() ? fmtNum(t.cumulative.gpa) + " / 4.00" : "n/a", Style::Gold)
                 << "    Credits earned: " << fmtNum(t.cumulative.earnedCredits, 1) << " of " << fmtNum(t.cumulative.gradedCredits, 1)
                 << " attempted    Standing: " << ctx.term.paint(std::string(toString(t.standing)), standingStyle(t.standing)) << "\n";
}

}  // namespace sms::ui
