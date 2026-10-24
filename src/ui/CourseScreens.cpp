#include "sms/ui/CourseScreens.hpp"

#include "sms/core/Exceptions.hpp"
#include "sms/models/Rules.hpp"
#include "sms/ui/Prompts.hpp"

namespace sms::ui {

namespace {
InputValidator::Checker titleChecker() {
    return [](const std::string& s) { return rules::checkCourseTitle(rules::collapseSpaces(s)); };
}
InputValidator::Checker deptChecker() {
    return [](const std::string& s) { return rules::checkDepartment(rules::collapseSpaces(s)); };
}
std::string trimNum(double v) {
    std::string s = text::formatFixed(v, 2);
    while (s.back() == '0') s.pop_back();
    if (s.back() == '.') s.pop_back();
    return s;
}
}  // namespace

void CourseScreens::menu(std::function<void(const std::function<void()>&)> guarded) {
    const bool admin = ctx_.user().role == Role::Admin;
    while (true) {
        ctx_.screen({"Courses"});
        std::vector<MenuItem> items = {{"1", "Course catalogue", "All courses with enrollment counts"},
                                       {"2", "Add course", "Create a catalogue entry"},
                                       {"3", "Edit course", "Title, credits, ECTS, department"},
                                       {"4", "Retire / reactivate", "Soft delete from the catalogue"}};
        if (admin) items.push_back({"5", "Delete permanently", "Removes the course and its enrollments"});
        items.push_back({"0", "Back", "Return to the dashboard"});
        ctx_.ui.menu("Courses", items);
        const std::string k = ctx_.in.menuKey();
        if (k == "0") return;
        if (k == "1") guarded([&] { list(); });
        else if (k == "2") guarded([&] { add(); });
        else if (k == "3") guarded([&] { edit(); });
        else if (k == "4") guarded([&] { toggleActive(); });
        else if (k == "5" && admin) guarded([&] { purge(); });
        else ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

void CourseScreens::list() {
    ctx_.screen({"Courses", "Catalogue"});
    const auto courses = ctx_.courses.list(ctx_.user(), true);
    TableBuilder t = ctx_.ui.table();
    t.title("Course catalogue")
        .column({"Code", Align::Left, 6, 0})
        .column({"Title", Align::Left, 8, 0})
        .column({"Department", Align::Left, 8, 0})
        .column({"Cr", Align::Right, 2, 0})
        .column({"ECTS", Align::Right, 4, 0})
        .column({"Enrolled", Align::Right, 8, 0})
        .column({"Status", Align::Left, 7, 0});
    for (const auto& c : courses) {
        const auto enrolled = ctx_.enrollments.forCourse(ctx_.user(), c.code).size();
        t.row(std::vector<Cell>{{c.code, {}}, {c.title, {}}, {c.department, {}}, {fmtNum(c.credits, 1), {}}, {fmtNum(c.ects, 1), {}},
                                {std::to_string(enrolled), {}}, {c.active ? "Active" : "Retired", ctx_.term.code(c.active ? Style::Green : Style::Amber)}});
    }
    t.footer(std::to_string(courses.size()) + " course(s)");
    ctx_.ui.out() << t.render();
    ctx_.in.pause();
}

void CourseScreens::add() {
    ctx_.screen({"Courses", "Add Course"});
    ctx_.ui.hint("Type /cancel at any prompt to abort.");
    Course c;
    c.code = rules::normalizeCourseCode(ctx_.in.line("Course code (e.g. COSC1011)", 14, false, [&](const std::string& v) -> std::optional<std::string> {
        const std::string code = rules::normalizeCourseCode(v);
        if (auto e = rules::checkCourseCode(code)) return e;
        if (ctx_.store.db().findCourse(code) != nullptr) return "That course code already exists.";
        return std::nullopt;
    }));
    c.title = rules::collapseSpaces(ctx_.in.line("Course title", 100, false, titleChecker()));
    c.credits = ctx_.in.number("Credit hours (0.5-12)", rules::kMinCredits, rules::kMaxCredits);
    c.ects = ctx_.in.number("ECTS (0-30)", rules::kMinEcts, rules::kMaxEcts);
    c.department = rules::collapseSpaces(ctx_.in.line("Department", 60, false, deptChecker()));
    ctx_.ui.keyValues("Review new course", {{"Code", c.code}, {"Title", c.title}, {"Credit hours", trimNum(c.credits)},
                                            {"ECTS", trimNum(c.ects)}, {"Department", c.department}});
    if (!ctx_.in.confirm("Save this course?", true)) {
        ctx_.ui.info("Nothing was saved.");
        return;
    }
    ctx_.courses.create(ctx_.user(), c);
    ctx_.ui.success("Course " + c.code + " created.");
    ctx_.in.pause();
}

void CourseScreens::edit() {
    ctx_.screen({"Courses", "Edit Course"});
    const std::string code = askCourseCode(ctx_);
    const auto current = ctx_.courses.find(ctx_.user(), code);
    if (!current) throw NotFoundException("Course " + code + " not found.");
    ctx_.ui.hint("Press Enter to keep the value shown in [brackets].");
    Course edited = *current;
    edited.title = rules::collapseSpaces(ctx_.in.line("Course title", 100, false, titleChecker(), current->title));
    double v = 0;
    edited.credits = *text::parseDouble(ctx_.in.line("Credit hours (0.5-12)", 10, false, [&](const std::string& s) { return InputValidator::checkNumber(s, rules::kMinCredits, rules::kMaxCredits, v); }, trimNum(current->credits)));
    edited.ects = *text::parseDouble(ctx_.in.line("ECTS (0-30)", 10, false, [&](const std::string& s) { return InputValidator::checkNumber(s, rules::kMinEcts, rules::kMaxEcts, v); }, trimNum(current->ects)));
    edited.department = rules::collapseSpaces(ctx_.in.line("Department", 60, false, deptChecker(), current->department));
    if (edited.title == current->title && edited.credits == current->credits && edited.ects == current->ects && edited.department == current->department) {
        ctx_.ui.info("No changes were made.");
        return;
    }
    if (current->credits != edited.credits) {
        ctx_.ui.warning("Changing credit hours re-weights GPAs of every student already graded in this course.");
    }
    if (!ctx_.in.confirm("Overwrite the stored course with these changes?", false)) {
        ctx_.ui.info("Edit discarded.");
        return;
    }
    ctx_.courses.update(ctx_.user(), edited);
    ctx_.ui.success("Course " + code + " updated.");
    ctx_.in.pause();
}

void CourseScreens::toggleActive() {
    ctx_.screen({"Courses", "Retire / Reactivate"});
    const std::string code = askCourseCode(ctx_);
    const auto c = ctx_.courses.find(ctx_.user(), code);
    if (!c) throw NotFoundException("Course " + code + " not found.");
    const bool retire = c->active;
    if (!ctx_.in.confirm((retire ? "Retire " : "Reactivate ") + c->code + " - " + c->title + "?", false)) {
        ctx_.ui.info("Cancelled - nothing changed.");
        return;
    }
    ctx_.courses.setActive(ctx_.user(), code, !retire);
    ctx_.ui.success(std::string("Course ") + code + (retire ? " retired (existing grades are kept; no new enrollments)." : " reactivated."));
    ctx_.in.pause();
}

void CourseScreens::purge() {
    ctx_.screen({"Courses", "Delete Permanently"});
    const std::string code = askCourseCode(ctx_);
    const auto c = ctx_.courses.find(ctx_.user(), code);
    if (!c) throw NotFoundException("Course " + code + " not found.");
    const std::size_t n = ctx_.enrollments.forCourse(ctx_.user(), code).size();
    if (!ctx_.in.confirmTyped("This PERMANENTLY deletes " + c->code + " and " + std::to_string(n) + " enrollment(s)/grade(s). A backup is taken first.", "DELETE")) {
        ctx_.ui.info("Confirmation text did not match - nothing deleted.");
        return;
    }
    ctx_.backups.snapshot(ctx_.user().username, "pre-delete");
    ctx_.courses.hardDelete(ctx_.user(), code);
    ctx_.ui.success("Course " + code + " permanently deleted.");
    ctx_.in.pause();
}

}  // namespace sms::ui
