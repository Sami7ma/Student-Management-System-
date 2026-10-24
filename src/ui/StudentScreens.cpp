#include "sms/ui/StudentScreens.hpp"

#include "sms/core/Exceptions.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Rules.hpp"
#include "sms/ui/Prompts.hpp"

namespace sms::ui {

namespace {

const char* sortName(SortKey k) {
    switch (k) {
        case SortKey::Name: return "name";
        case SortKey::Id: return "student ID";
        case SortKey::Gpa: return "GPA";
        case SortKey::EnrolledOn: return "enrollment date";
        case SortKey::Department: return "department";
    }
    return "";
}

InputValidator::Checker emailChecker() {
    return [](const std::string& s) { return rules::checkEmail(s); };
}
InputValidator::Checker phoneChecker() {
    return [](const std::string& s) { return rules::checkPhone(rules::normalizePhone(s)); };
}
InputValidator::Checker departmentChecker() {
    return [](const std::string& s) { return rules::checkDepartment(rules::collapseSpaces(s)); };
}
InputValidator::Checker nameChecker() {
    return [](const std::string& s) { return rules::checkPersonName(rules::collapseSpaces(s)); };
}
InputValidator::Checker dobChecker() {
    return [](const std::string& s) { return rules::checkDateOfBirth(s, timeutil::today()); };
}

/// Edit helper: shows the current value as default; "-" clears an optional field.
std::string editField(AppContext& ctx, const std::string& label, const std::string& current, std::size_t maxLen,
                      const InputValidator::Checker& check, bool clearable) {
    const std::string shown = label + (clearable && !current.empty() ? " ('-' to clear)" : "");
    const std::string v = ctx.in.line(shown, maxLen, clearable, [&](const std::string& s) -> std::optional<std::string> {
        if (clearable && s == "-") return std::nullopt;
        return check(s);
    }, current);
    return v == "-" ? std::string() : v;
}

void studentRowsTable(AppContext& ctx, const std::vector<StudentSummary>& rows, std::size_t begin, std::size_t end,
                      const std::string& title, const std::string& footer, bool showStatus) {
    TableBuilder t = ctx.ui.table();
    t.title(title)
        .column({"#", Align::Right, 2, 0})
        .column({"Student ID", Align::Left, 10, 0})
        .column({"Name", Align::Left, 8, 0})
        .column({"Department", Align::Left, 8, 0})
        .column({"GPA", Align::Right, 4, 0})
        .column({"Standing", Align::Left, 8, 0});
    if (showStatus) t.column({"Status", Align::Left, 6, 0});
    for (std::size_t i = begin; i < end; ++i) {
        const StudentSummary& r = rows[i];
        std::vector<Cell> cells = {{std::to_string(i + 1), ctx.term.code(Style::Gray)},
                                   {r.student.id, {}},
                                   {r.student.name, {}},
                                   {r.student.department, {}},
                                   {r.cumulative.hasGrades() ? fmtNum(r.cumulative.gpa) : "-", {}},
                                   {std::string(toString(r.standing)), ctx.term.code(standingStyle(r.standing))}};
        if (showStatus) cells.push_back({r.student.active ? "Active" : "Deactivated", ctx.term.code(r.student.active ? Style::Green : Style::Amber)});
        t.row(std::move(cells));
    }
    t.footer(footer);
    ctx.ui.out() << t.render();
}

}  // namespace

void StudentScreens::menu(std::function<void(const std::function<void()>&)> guarded) {
    const bool admin = ctx_.user().role == Role::Admin;
    while (true) {
        ctx_.screen({"Students"});
        std::vector<MenuItem> items = {{"1", "Browse students", "Paginated list, sortable"},
                                       {"2", "Search / filter", "Name, ID, department, GPA range"},
                                       {"3", "View profile", "Details, enrollments and standing"},
                                       {"4", "Add student", "Create a new record"},
                                       {"5", "Edit student", "Update contact / academic details"},
                                       {"6", "Deactivate student", "Soft delete (can be restored)"}};
        if (admin) items.push_back({"7", "Recycle bin", "Restore or permanently delete"});
        items.push_back({"0", "Back", "Return to the dashboard"});
        ctx_.ui.menu("Students", items);
        const std::string k = ctx_.in.menuKey();
        if (k == "0") return;
        if (k == "1") guarded([&] { browse(SearchCriteria{}, SortSpec{}, "All students"); });
        else if (k == "2") guarded([&] { search(); });
        else if (k == "3") guarded([&] { viewProfile(); });
        else if (k == "4") guarded([&] { add(); });
        else if (k == "5") guarded([&] { edit(); });
        else if (k == "6") guarded([&] { deactivate(); });
        else if (k == "7" && admin) guarded([&] { recycleBin(); });
        else ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

SortSpec StudentScreens::askSort() {
    ctx_.ui.line(" Sort by: 1) Name  2) Student ID  3) GPA  4) Enrollment date  5) Department");
    const int choice = ctx_.in.integer("Sort key", 1, 5);
    const std::string dir = text::toLower(ctx_.in.line("Direction (a = ascending, d = descending)", 1, false,
        [](const std::string& s) -> std::optional<std::string> {
            const std::string l = text::toLower(s);
            if (l == "a" || l == "d") return std::nullopt;
            return "Enter a or d.";
        }, choice == 3 ? "d" : "a"));
    static const SortKey keys[] = {SortKey::Name, SortKey::Id, SortKey::Gpa, SortKey::EnrolledOn, SortKey::Department};
    return SortSpec{keys[choice - 1], dir == "a"};
}

void StudentScreens::browse(SearchCriteria criteria, SortSpec sort, const std::string& title) {
    Pager pager(ctx_, std::max<std::size_t>(5, ctx_.term.height() > 22 ? ctx_.term.height() - 20 : 5));
    while (true) {
        const auto rows = ctx_.students.search(ctx_.user(), criteria, sort);
        const std::string footerBase = std::to_string(rows.size()) + " record(s)  |  sorted by " + sortName(sort.key) +
                                       (sort.ascending ? " (asc)" : " (desc)");
        const std::string cmd = pager.run(
            rows.size(),
            [&](std::size_t begin, std::size_t end, std::size_t page, std::size_t pages) {
                ctx_.screen({"Students", title});
                studentRowsTable(ctx_, rows, begin, end, title,
                                 "Page " + std::to_string(page) + "/" + std::to_string(pages) + "  |  " + footerBase,
                                 criteria.includeInactive || criteria.onlyInactive);
            },
            "[S]ort  [V]iew  [X] CSV  [M]arkdown");
        if (cmd == "q" || cmd == "quit" || cmd == "0") return;
        try {
            if (cmd == "s") {
                sort = askSort();
                pager.reset();
            } else if (cmd == "v") {
                if (rows.empty()) {
                    ctx_.ui.fieldError("Nothing to view.");
                    continue;
                }
                const std::string ref = ctx_.in.line("Row number or student ID", 20, false);
                std::string id = rules::normalizeStudentId(ref);
                if (const auto n = text::parseInteger(ref)) {
                    if (*n < 1 || static_cast<std::size_t>(*n) > rows.size()) throw ValidationException("No such row.");
                    id = rows[static_cast<std::size_t>(*n) - 1].student.id;
                }
                ctx_.screen({"Students", "Profile"});
                renderProfile(ctx_, id);
                ctx_.in.pause();
            } else if (cmd == "x" || cmd == "m") {
                const auto path = cmd == "x" ? ctx_.exports.exportStudentsCsv(ctx_.user(), rows)
                                             : ctx_.exports.exportStudentsMarkdown(ctx_.user(), rows);
                ctx_.ui.success("Exported " + std::to_string(rows.size()) + " record(s) to " + path.string());
                ctx_.in.pause();
            } else {
                ctx_.ui.fieldError("Unknown command '" + text::sanitizeForLog(cmd, 20) + "'.");
            }
        } catch (const OperationCancelled&) {
            ctx_.ui.info("Cancelled.");
        } catch (const ValidationException& e) {
            ctx_.ui.warning(e.what());
        } catch (const NotFoundException& e) {
            ctx_.ui.warning(e.what());
        } catch (const AuthException& e) {
            ctx_.ui.error(e.what());
            ctx_.in.pause();
        }
    }
}

void StudentScreens::search() {
    ctx_.screen({"Students", "Search"});
    ctx_.ui.hint("Leave a field blank to ignore it. Matching is case-insensitive and partial.");
    const auto depts = ctx_.students.departments(ctx_.user());
    if (!depts.empty()) {
        std::string list;
        for (const auto& d : depts) list += (list.empty() ? "" : ", ") + d;
        ctx_.ui.hint("Departments: " + list);
    }
    SearchCriteria c;
    c.nameContains = ctx_.in.line("Name contains", 100, true);
    c.idContains = ctx_.in.line("ID contains", 20, true);
    c.department = ctx_.in.line("Department contains", 60, true);
    c.gpaMin = ctx_.in.optionalNumber("Minimum GPA (0-4)", 0.0, 4.0);
    c.gpaMax = ctx_.in.optionalNumber("Maximum GPA", c.gpaMin.value_or(0.0), 4.0);
    c.includeInactive = ctx_.in.confirm("Include deactivated students?", false);
    const SortSpec sort = askSort();
    browse(c, sort, "Search results");
}

void StudentScreens::viewProfile() {
    ctx_.screen({"Students", "View Profile"});
    const std::string id = askStudentId(ctx_);
    ctx_.screen({"Students", "View Profile", id});
    renderProfile(ctx_, id);
    ctx_.in.pause();
}

void StudentScreens::add() {
    ctx_.screen({"Students", "Add Student"});
    ctx_.ui.hint("Type /cancel at any prompt to abort.");
    Student s;
    s.id = rules::normalizeStudentId(ctx_.in.line("Student ID (e.g. NSE/1001/16)", 20, false, [&](const std::string& v) -> std::optional<std::string> {
        const std::string id = rules::normalizeStudentId(v);
        if (auto e = rules::checkStudentId(id)) return e;
        if (ctx_.store.db().findStudent(id) != nullptr) return "A student with that ID already exists.";
        return std::nullopt;
    }));
    s.name = rules::collapseSpaces(ctx_.in.line("Full name", 100, false, nameChecker()));
    s.email = ctx_.in.line("Email", 254, true, emailChecker());
    s.phone = rules::normalizePhone(ctx_.in.line("Phone", 24, true, phoneChecker()));
    s.department = rules::collapseSpaces(ctx_.in.line("Department", 60, false, departmentChecker()));
    const std::string dob = ctx_.in.line("Date of birth YYYY-MM-DD", 10, true, dobChecker());
    if (!dob.empty()) s.dateOfBirth = dob;
    s.enrolledOn = ctx_.in.line("Enrollment date", 10, false, [](const std::string& v) { return rules::checkEnrollmentDate(v, timeutil::today()); }, timeutil::today());

    ctx_.ui.keyValues("Review new student", {{"Student ID", s.id}, {"Name", s.name}, {"Email", s.email.empty() ? "-" : s.email},
                                             {"Phone", s.phone.empty() ? "-" : s.phone}, {"Department", s.department},
                                             {"Date of birth", s.dateOfBirth.value_or("-")}, {"Enrolled on", s.enrolledOn}});
    if (!ctx_.in.confirm("Save this student?", true)) {
        ctx_.ui.info("Nothing was saved.");
        return;
    }
    const Student created = ctx_.students.create(ctx_.user(), s);
    ctx_.ui.success("Student " + created.id + " (" + created.name + ") created. Use Enrollments & Grades to register courses.");
    ctx_.in.pause();
}

void StudentScreens::edit() {
    ctx_.screen({"Students", "Edit Record"});
    const std::string id = askStudentId(ctx_);
    const auto current = ctx_.students.find(ctx_.user(), id);
    if (!current) throw NotFoundException("Student " + id + " not found.");
    if (!current->active) throw ValidationException("That student is deactivated; restore it from the recycle bin first.");
    ctx_.screen({"Students", "Edit Record", id});
    ctx_.ui.hint("Press Enter to keep the current value shown in [brackets].");

    Student edited = *current;
    edited.name = rules::collapseSpaces(editField(ctx_, "Full name", current->name, 100, nameChecker(), false));
    edited.email = editField(ctx_, "Email", current->email, 254, emailChecker(), true);
    edited.phone = rules::normalizePhone(editField(ctx_, "Phone", current->phone, 24, phoneChecker(), true));
    edited.department = rules::collapseSpaces(editField(ctx_, "Department", current->department, 60, departmentChecker(), false));
    const std::string dob = editField(ctx_, "Date of birth", current->dateOfBirth.value_or(""), 10, dobChecker(), true);
    edited.dateOfBirth = dob.empty() ? std::nullopt : std::optional<std::string>(dob);

    std::vector<std::pair<std::string, std::string>> diff;
    auto note = [&](const char* label, const std::string& a, const std::string& b) {
        if (a != b) diff.emplace_back(label, (a.empty() ? "-" : a) + "  ->  " + (b.empty() ? "-" : b));
    };
    note("Name", current->name, edited.name);
    note("Email", current->email, edited.email);
    note("Phone", current->phone, edited.phone);
    note("Department", current->department, edited.department);
    note("Date of birth", current->dateOfBirth.value_or(""), edited.dateOfBirth.value_or(""));
    if (diff.empty()) {
        ctx_.ui.info("No changes were made.");
        return;
    }
    ctx_.ui.keyValues("Pending changes", diff);
    if (!ctx_.in.confirm("Overwrite the stored record with these changes?", false)) {
        ctx_.ui.info("Edit discarded.");
        return;
    }
    ctx_.students.update(ctx_.user(), edited);
    ctx_.ui.success("Student " + id + " updated.");
    ctx_.in.pause();
}

void StudentScreens::deactivate() {
    ctx_.screen({"Students", "Deactivate"});
    const std::string id = askStudentId(ctx_);
    ctx_.screen({"Students", "Deactivate", id});
    renderProfile(ctx_, id);
    if (!ctx_.in.confirm("Deactivate this student? They disappear from lists and any login is disabled", false)) {
        ctx_.ui.info("Cancelled - nothing changed.");
        return;
    }
    ctx_.students.softDelete(ctx_.user(), id);
    ctx_.ui.success("Student " + id + " deactivated. An administrator can restore the record.");
    ctx_.in.pause();
}

void StudentScreens::recycleBin() {
    while (true) {
        ctx_.screen({"Students", "Recycle Bin"});
        SearchCriteria c;
        c.onlyInactive = true;
        const auto rows = ctx_.students.search(ctx_.user(), c, SortSpec{});
        studentRowsTable(ctx_, rows, 0, rows.size(), "Deactivated students", std::to_string(rows.size()) + " record(s)", true);
        ctx_.ui.hint("[R]estore  [H]ard delete (permanent)  [Q]uit");
        const std::string cmd = ctx_.in.menuKey("Command");
        if (cmd == "q" || cmd == "0") return;
        try {
            if (cmd == "r") {
                const std::string id = askStudentId(ctx_);
                ctx_.students.restore(ctx_.user(), id);
                ctx_.ui.success("Student " + id + " restored. Linked login (if any) stays disabled until re-enabled.");
            } else if (cmd == "h") {
                const std::string id = askStudentId(ctx_);
                if (ctx_.in.confirmTyped("This PERMANENTLY deletes " + id + ", all enrollments and any linked login. A backup is taken first.", "DELETE")) {
                    ctx_.backups.snapshot(ctx_.user().username, "pre-delete");
                    ctx_.students.hardDelete(ctx_.user(), id);
                    ctx_.ui.success("Student " + id + " permanently deleted.");
                } else {
                    ctx_.ui.info("Confirmation text did not match - nothing deleted.");
                }
            } else {
                ctx_.ui.fieldError("Unknown command.");
            }
        } catch (const OperationCancelled&) {
            ctx_.ui.info("Cancelled.");
        } catch (const ValidationException& e) {
            ctx_.ui.warning(e.what());
        } catch (const NotFoundException& e) {
            ctx_.ui.warning(e.what());
        }
        ctx_.in.pause();
    }
}

}  // namespace sms::ui
