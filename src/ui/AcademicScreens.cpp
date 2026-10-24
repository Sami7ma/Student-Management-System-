#include "sms/ui/AcademicScreens.hpp"

#include "sms/core/Exceptions.hpp"
#include "sms/models/Grading.hpp"
#include "sms/models/Rules.hpp"
#include "sms/ui/Prompts.hpp"

namespace sms::ui {

// ============================ Enrollments & grades ============================

void EnrollmentScreens::menu(std::function<void(const std::function<void()>&)> guarded) {
    while (true) {
        ctx_.screen({"Enrollments & Grades"});
        ctx_.ui.menu("Enrollments & Grades",
                     {{"1", "Enroll student in course", "Optionally record the mark immediately"},
                      {"2", "Record / update grade", "Percentage mark 0-100 -> letter grade"},
                      {"3", "Clear a grade", "Mark the enrollment as ungraded again"},
                      {"4", "Remove enrollment", "Withdraw the student from a course"},
                      {"0", "Back", "Return to the dashboard"}});
        const std::string k = ctx_.in.menuKey();
        if (k == "0") return;
        if (k == "1") guarded([&] { enroll(); });
        else if (k == "2") guarded([&] { recordGrade(); });
        else if (k == "3") guarded([&] { clearGrade(); });
        else if (k == "4") guarded([&] { remove(); });
        else ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

Enrollment EnrollmentScreens::pick(const std::string& studentId, bool gradedOnly) {
    const auto list = ctx_.enrollments.forStudent(ctx_.user(), studentId);
    std::vector<Enrollment> shown;
    for (const auto& e : list) {
        if (!gradedOnly || e.mark) shown.push_back(e);
    }
    if (shown.empty()) throw NotFoundException(gradedOnly ? "This student has no graded enrollments." : "This student has no enrollments.");
    TableBuilder t = ctx_.ui.table();
    t.title("Enrollments of " + studentId)
        .column({"#", Align::Right, 2, 0})
        .column({"Term", Align::Left, 4, 0})
        .column({"Code", Align::Left, 6, 0})
        .column({"Course", Align::Left, 8, 0})
        .column({"Mark", Align::Right, 4, 0})
        .column({"Grade", Align::Center, 5, 0});
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const Enrollment& e = shown[i];
        const auto c = ctx_.courses.find(ctx_.user(), e.courseCode);
        t.row(std::vector<std::string>{std::to_string(i + 1), e.term, e.courseCode, c ? c->title : "?",
                                       e.mark ? fmtNum(*e.mark) : "-", e.mark ? grading::letterFromMark(*e.mark) : "-"});
    }
    ctx_.ui.out() << t.render();
    const int n = ctx_.in.integer("Enrollment number", 1, static_cast<int>(shown.size()));
    return shown[static_cast<std::size_t>(n) - 1];
}

void EnrollmentScreens::enroll() {
    ctx_.screen({"Enrollments & Grades", "Enroll"});
    ctx_.ui.hint("Type /cancel at any prompt to abort.");
    const std::string sid = askStudentId(ctx_);
    const auto student = ctx_.students.find(ctx_.user(), sid);
    if (!student) throw NotFoundException("Student " + sid + " not found.");
    if (!student->active) throw ValidationException("That student is deactivated.");
    ctx_.ui.info("Student: " + student->name + " (" + student->department + ")");

    const auto active = ctx_.courses.list(ctx_.user(), false);
    if (active.empty()) throw ValidationException("The course catalogue is empty - add a course first.");
    std::string codes;
    for (const auto& c : active) codes += (codes.empty() ? "" : ", ") + c.code;
    ctx_.ui.hint("Active courses: " + text::truncate(codes, ctx_.ui.contentWidth() - 20));

    const std::string code = askCourseCode(ctx_);
    const auto course = ctx_.courses.find(ctx_.user(), code);
    if (!course) throw NotFoundException("Course " + code + " not found.");
    ctx_.ui.info("Course: " + course->title + " (" + fmtNum(course->credits, 1) + " credit hours)");
    const std::string term = askTerm(ctx_);
    const auto mark = ctx_.in.optionalNumber("Mark % (0-100, blank = not graded yet)", 0.0, 100.0);
    ctx_.enrollments.enroll(ctx_.user(), sid, code, term, mark);
    ctx_.ui.success(student->name + " enrolled in " + code + " for " + term +
                    (mark ? " with grade " + grading::letterFromMark(*mark) : std::string(" (ungraded)")) + ".");
    ctx_.in.pause();
}

void EnrollmentScreens::recordGrade() {
    ctx_.screen({"Enrollments & Grades", "Record Grade"});
    const std::string sid = askStudentId(ctx_);
    const Enrollment e = pick(sid, false);
    if (e.mark) ctx_.ui.warning("Existing mark " + fmtNum(*e.mark) + " (" + grading::letterFromMark(*e.mark) + ") will be replaced.");
    const double mark = ctx_.in.number("New mark % (0-100)", 0.0, 100.0);
    if (e.mark && !ctx_.in.confirm("Overwrite the existing mark?", false)) {
        ctx_.ui.info("Cancelled - mark unchanged.");
        return;
    }
    ctx_.enrollments.recordMark(ctx_.user(), sid, e.courseCode, e.term, mark);
    const StudentSummary sum = ctx_.analytics.summary(ctx_.user(), sid);
    ctx_.ui.success("Recorded " + fmtNum(mark) + "% (" + grading::letterFromMark(mark) + "). Cumulative GPA is now " + fmtNum(sum.cumulative.gpa) + ".");
    ctx_.in.pause();
}

void EnrollmentScreens::clearGrade() {
    ctx_.screen({"Enrollments & Grades", "Clear Grade"});
    const std::string sid = askStudentId(ctx_);
    const Enrollment e = pick(sid, true);
    if (!ctx_.in.confirm("Clear the grade for " + e.courseCode + " (" + e.term + ")?", false)) {
        ctx_.ui.info("Cancelled - nothing changed.");
        return;
    }
    ctx_.enrollments.clearMark(ctx_.user(), sid, e.courseCode, e.term);
    ctx_.ui.success("Grade cleared; the enrollment remains and no longer counts toward GPA.");
    ctx_.in.pause();
}

void EnrollmentScreens::remove() {
    ctx_.screen({"Enrollments & Grades", "Remove Enrollment"});
    const std::string sid = askStudentId(ctx_);
    const Enrollment e = pick(sid, false);
    if (e.mark) ctx_.ui.warning("This enrollment already has a recorded grade, which will be deleted with it.");
    if (!ctx_.in.confirm("Remove " + sid + " from " + e.courseCode + " (" + e.term + ")?", false)) {
        ctx_.ui.info("Cancelled - nothing changed.");
        return;
    }
    ctx_.enrollments.withdraw(ctx_.user(), sid, e.courseCode, e.term);
    ctx_.ui.success("Enrollment removed.");
    ctx_.in.pause();
}

// ================================ Analytics =================================

void AnalyticsScreens::menu(std::function<void(const std::function<void()>&)> guarded) {
    while (true) {
        ctx_.screen({"Analytics"});
        ctx_.ui.menu("Analytics",
                     {{"1", "Overview dashboard", "Totals, average GPA, standing distribution"},
                      {"2", "Course statistics", "Mean, median, pass/fail rates, grade histogram"},
                      {"3", "Department summary", "Students and average GPA per department"},
                      {"4", "Top students", "Highest cumulative GPA"},
                      {"5", "Student transcript", "Printable transcript (+ save to file)"},
                      {"0", "Back", "Return to the dashboard"}});
        const std::string k = ctx_.in.menuKey();
        if (k == "0") return;
        if (k == "1") guarded([&] { overview(); });
        else if (k == "2") guarded([&] { courseStats(); });
        else if (k == "3") guarded([&] { departmentStats(); });
        else if (k == "4") guarded([&] { topStudents(); });
        else if (k == "5") guarded([&] {
            ctx_.screen({"Analytics", "Transcript"});
            transcript(askStudentId(ctx_));
        });
        else ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

void AnalyticsScreens::overview() {
    ctx_.screen({"Analytics", "Overview"});
    const Overview o = ctx_.analytics.overview(ctx_.user());
    TableBuilder t = ctx_.ui.table();
    t.title("System overview").column({"Metric", Align::Left, 20, 0}).column({"Value", Align::Right, 8, 0});
    t.row(std::vector<std::string>{"Active students", std::to_string(o.activeStudents)});
    t.row(std::vector<std::string>{"Deactivated students", std::to_string(o.inactiveStudents)});
    t.row(std::vector<std::string>{"Active courses", std::to_string(o.activeCourses)});
    t.row(std::vector<std::string>{"Enrollments (graded)", std::to_string(o.enrollments) + " (" + std::to_string(o.gradedEnrollments) + ")"});
    t.row(std::vector<std::string>{"Average cumulative GPA", o.averageGpa > 0 ? fmtNum(o.averageGpa) : "n/a"});
    ctx_.ui.out() << t.render();
    const double graded = static_cast<double>(o.deansList + o.goodStanding + o.probation);
    ctx_.ui.barChart("Academic standing (students with grades)",
                     {{"Dean's List", static_cast<double>(o.deansList)},
                      {"Good Standing", static_cast<double>(o.goodStanding)},
                      {"Probation", static_cast<double>(o.probation)}},
                     graded);
    ctx_.in.pause();
}

void AnalyticsScreens::courseStats() {
    ctx_.screen({"Analytics", "Course Statistics"});
    const auto all = ctx_.analytics.allCourseStats(ctx_.user());
    TableBuilder t = ctx_.ui.table();
    t.title("Course statistics (marks in %)")
        .column({"Code", Align::Left, 6, 0})
        .column({"Course", Align::Left, 8, 0})
        .column({"Enr", Align::Right, 3, 0})
        .column({"Gr", Align::Right, 2, 0})
        .column({"Mean", Align::Right, 4, 0})
        .column({"Med", Align::Right, 4, 0})
        .column({"SD", Align::Right, 4, 0})
        .column({"Min", Align::Right, 4, 0})
        .column({"Max", Align::Right, 4, 0})
        .column({"Pass%", Align::Right, 5, 0})
        .column({"Fail%", Align::Right, 5, 0});
    for (const auto& s : all) {
        const bool g = s.graded > 0;
        t.row(std::vector<Cell>{{s.course.code, {}}, {s.course.title, {}}, {std::to_string(s.enrolled), {}}, {std::to_string(s.graded), {}},
                                {g ? fmtNum(s.mean, 1) : "-", {}}, {g ? fmtNum(s.median, 1) : "-", {}}, {g ? fmtNum(s.stddev, 1) : "-", {}},
                                {g ? fmtNum(s.minMark, 1) : "-", {}}, {g ? fmtNum(s.maxMark, 1) : "-", {}},
                                {g ? fmtNum(s.passRate, 1) : "-", ctx_.term.code(Style::Green)},
                                {g ? fmtNum(s.failRate, 1) : "-", ctx_.term.code(s.failRate > 0 ? Style::Red : Style::Gray)}});
    }
    t.footer("Pass = mark >= 40 (grade D or better). SD = population standard deviation.");
    ctx_.ui.out() << t.render();

    const std::string raw = ctx_.in.line("Course code for the grade histogram (blank to skip)", 14, true,
                                         [](const std::string& s) { return rules::checkCourseCode(rules::normalizeCourseCode(s)); });
    if (raw.empty()) return;
    const CourseStats s = ctx_.analytics.courseStats(ctx_.user(), rules::normalizeCourseCode(raw));
    std::vector<std::pair<std::string, double>> bars;
    for (const auto& [letter, count] : s.distribution) bars.emplace_back(letter, static_cast<double>(count));
    ctx_.ui.barChart("Grade distribution - " + s.course.code + " " + s.course.title, bars, static_cast<double>(s.graded));
    ctx_.in.pause();
}

void AnalyticsScreens::departmentStats() {
    ctx_.screen({"Analytics", "Departments"});
    const auto stats = ctx_.analytics.departmentStats(ctx_.user());
    TableBuilder t = ctx_.ui.table();
    t.title("Department summary (active students)")
        .column({"Department", Align::Left, 10, 0})
        .column({"Students", Align::Right, 8, 0})
        .column({"Graded", Align::Right, 6, 0})
        .column({"Avg GPA", Align::Right, 7, 0})
        .column({"Dean's", Align::Right, 6, 0})
        .column({"Good", Align::Right, 4, 0})
        .column({"Probation", Align::Right, 9, 0});
    for (const auto& d : stats) {
        t.row(std::vector<std::string>{d.department, std::to_string(d.students), std::to_string(d.graded),
                                       d.graded ? fmtNum(d.averageGpa) : "-", std::to_string(d.deansList),
                                       std::to_string(d.goodStanding), std::to_string(d.probation)});
    }
    ctx_.ui.out() << t.render();
    ctx_.in.pause();
}

void AnalyticsScreens::topStudents() {
    ctx_.screen({"Analytics", "Top Students"});
    const int n = ctx_.in.optionalInteger("How many students (1-50, blank = 10)", 1, 50).value_or(10);
    const auto top = ctx_.analytics.topStudents(ctx_.user(), static_cast<std::size_t>(n));
    TableBuilder t = ctx_.ui.table();
    t.title("Top students by cumulative GPA")
        .column({"Rank", Align::Right, 4, 0})
        .column({"Student ID", Align::Left, 10, 0})
        .column({"Name", Align::Left, 8, 0})
        .column({"Department", Align::Left, 8, 0})
        .column({"GPA", Align::Right, 4, 0})
        .column({"Standing", Align::Left, 8, 0});
    for (std::size_t i = 0; i < top.size(); ++i) {
        const auto& r = top[i];
        t.row(std::vector<Cell>{{std::to_string(i + 1), {}}, {r.student.id, {}}, {r.student.name, {}}, {r.student.department, {}},
                                {fmtNum(r.cumulative.gpa), ctx_.term.code(Style::Gold)},
                                {std::string(toString(r.standing)), ctx_.term.code(standingStyle(r.standing))}});
    }
    ctx_.ui.out() << t.render();
    ctx_.in.pause();
}

void AnalyticsScreens::transcript(const std::string& studentId) {
    const Transcript t = ctx_.analytics.transcript(ctx_.user(), studentId);
    ctx_.screen({"Transcript", studentId});
    renderTranscript(ctx_, t);
    if (ctx_.in.confirm("Save a printable text copy?", false)) {
        const auto path = ctx_.exports.exportTranscript(ctx_.user(), studentId);
        ctx_.ui.success("Transcript saved to " + path.string());
    }
    ctx_.in.pause();
}

void AnalyticsScreens::reports(std::function<void(const std::function<void()>&)> guarded) {
    while (true) {
        ctx_.screen({"Reports & Export"});
        ctx_.ui.menu("Reports & Export",
                     {{"1", "Students -> CSV", "All active students with GPA and standing"},
                      {"2", "Students -> Markdown", "Same data as a Markdown table"},
                      {"3", "Course statistics -> CSV", "Per-course statistics"},
                      {"4", "Course statistics -> Markdown", "Per-course statistics as Markdown"},
                      {"5", "Student transcript -> text file", "Printable transcript"},
                      {"0", "Back", "Return to the dashboard"}});
        const std::string k = ctx_.in.menuKey();
        if (k == "0") return;
        auto studentRows = [&] { return ctx_.students.search(ctx_.user(), SearchCriteria{}, SortSpec{}); };
        if (k == "1") guarded([&] { const auto p = ctx_.exports.exportStudentsCsv(ctx_.user(), studentRows()); ctx_.ui.success("Saved " + p.string()); });
        else if (k == "2") guarded([&] { const auto p = ctx_.exports.exportStudentsMarkdown(ctx_.user(), studentRows()); ctx_.ui.success("Saved " + p.string()); });
        else if (k == "3") guarded([&] { const auto p = ctx_.exports.exportCourseStatsCsv(ctx_.user()); ctx_.ui.success("Saved " + p.string()); });
        else if (k == "4") guarded([&] { const auto p = ctx_.exports.exportCourseStatsMarkdown(ctx_.user()); ctx_.ui.success("Saved " + p.string()); });
        else if (k == "5") guarded([&] { const auto p = ctx_.exports.exportTranscript(ctx_.user(), askStudentId(ctx_)); ctx_.ui.success("Saved " + p.string()); });
        else ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

}  // namespace sms::ui
