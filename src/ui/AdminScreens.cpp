#include "sms/ui/AdminScreens.hpp"

#include "sms/core/Exceptions.hpp"
#include "sms/models/Rules.hpp"
#include "sms/ui/Prompts.hpp"

namespace sms::ui {

void AdminScreens::menu(std::function<void(const std::function<void()>&)> guarded) {
    while (true) {
        ctx_.screen({"Administration"});
        ctx_.ui.menu("Administration",
                     {{"1", "User accounts", "Create, disable, reset passwords"},
                      {"2", "Backups", "Snapshots and validated restore"},
                      {"3", "Security audit log", "Recent events + tamper check"},
                      {"4", "Import legacy students.dat", "Migrate data from the original program"},
                      {"5", "System information", "Paths and record counts"},
                      {"0", "Back", "Return to the dashboard"}});
        const std::string k = ctx_.in.menuKey();
        if (k == "0") return;
        if (k == "1") guarded([&] { users(); });
        else if (k == "2") guarded([&] { backups(); });
        else if (k == "3") guarded([&] { auditLog(); });
        else if (k == "4") guarded([&] { importLegacy(); });
        else if (k == "5") guarded([&] { systemInfo(); });
        else ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

void AdminScreens::users() {
    while (true) {
        ctx_.screen({"Administration", "User Accounts"});
        const auto list = ctx_.auth.listUsers(ctx_.user());
        TableBuilder t = ctx_.ui.table();
        t.title("User accounts")
            .column({"Username", Align::Left, 8, 0})
            .column({"Role", Align::Left, 7, 0})
            .column({"Linked student", Align::Left, 8, 0})
            .column({"Status", Align::Left, 8, 0})
            .column({"Must change pw", Align::Center, 6, 0});
        for (const auto& u : list) {
            t.row(std::vector<Cell>{{u.username, {}}, {std::string(toString(u.role)), {}}, {u.linkedStudentId.empty() ? "-" : u.linkedStudentId, {}},
                                    {u.active ? "Active" : "Disabled", ctx_.term.code(u.active ? Style::Green : Style::Amber)},
                                    {u.mustChangePassword ? "yes" : "no", {}}});
        }
        t.footer(std::to_string(list.size()) + " account(s)");
        ctx_.ui.out() << t.render();
        ctx_.ui.hint("[C]reate  [R]eset password  [D] disable/enable  [Q]uit");
        const std::string cmd = ctx_.in.menuKey("Command");
        if (cmd == "q" || cmd == "0") return;
        try {
            if (cmd == "c") {
                const std::string name = ctx_.in.line("New username", 24, false, [&](const std::string& s) -> std::optional<std::string> {
                    const std::string u = rules::normalizeUsername(s);
                    if (auto e = rules::checkUsername(u)) return e;
                    if (ctx_.store.db().findUser(u) != nullptr) return "That username is taken.";
                    return std::nullopt;
                });
                ctx_.ui.line(" Role: 1) Admin  2) Faculty/Staff  3) Student");
                const int r = ctx_.in.integer("Role", 1, 3);
                const Role role = r == 1 ? Role::Admin : (r == 2 ? Role::Faculty : Role::Student);
                const std::string sid = role == Role::Student ? askStudentId(ctx_, "Linked student ID") : std::string();
                const std::string pw = ctx_.in.newSecret("Temporary password");
                ctx_.auth.createUser(ctx_.user(), name, role, pw, sid);
                ctx_.ui.success("Account created. The user must change the password at first login.");
            } else if (cmd == "r") {
                const std::string name = ctx_.in.line("Username", 24, false);
                const std::string pw = ctx_.in.newSecret("New temporary password");
                ctx_.auth.resetPassword(ctx_.user(), name, pw);
                ctx_.ui.success("Password reset; the user must change it at next login.");
            } else if (cmd == "d") {
                const std::string name = rules::normalizeUsername(ctx_.in.line("Username", 24, false));
                const auto it = std::find_if(list.begin(), list.end(), [&](const User& u) { return u.username == name; });
                if (it == list.end()) throw NotFoundException("User '" + name + "' not found.");
                const bool enable = !it->active;
                if (ctx_.in.confirm(std::string(enable ? "Enable " : "Disable ") + name + "?", false)) {
                    ctx_.auth.setActive(ctx_.user(), name, enable);
                    ctx_.ui.success("Account " + name + (enable ? " enabled." : " disabled."));
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

void AdminScreens::backups() {
    while (true) {
        ctx_.screen({"Administration", "Backups"});
        const auto list = ctx_.backups.list(ctx_.user());
        TableBuilder t = ctx_.ui.table();
        t.title("Snapshots in " + ctx_.backups.directory().string())
            .column({"#", Align::Right, 2, 0})
            .column({"File", Align::Left, 10, 0})
            .column({"Size", Align::Right, 8, 0});
        for (std::size_t i = 0; i < list.size(); ++i) {
            t.row(std::vector<std::string>{std::to_string(i + 1), list[i].name, std::to_string(list[i].bytes) + " B"});
        }
        t.footer("Newest first. The newest 20 snapshots are kept automatically.");
        ctx_.ui.out() << t.render();
        ctx_.ui.hint("[C]reate snapshot  [R]estore  [Q]uit");
        const std::string cmd = ctx_.in.menuKey("Command");
        if (cmd == "q" || cmd == "0") return;
        try {
            if (cmd == "c") {
                const auto p = ctx_.backups.createManual(ctx_.user());
                ctx_.ui.success("Snapshot created: " + p.filename().string());
            } else if (cmd == "r") {
                if (list.empty()) throw ValidationException("There are no snapshots to restore.");
                const int n = ctx_.in.integer("Snapshot number", 1, static_cast<int>(list.size()));
                const auto& name = list[static_cast<std::size_t>(n) - 1].name;
                ctx_.ui.warning("Restoring replaces ALL current data (users included) with snapshot " + name +
                                ". The current state is saved first. You are signed out if your account is not in the snapshot.");
                if (ctx_.in.confirmTyped("Confirm restore", "RESTORE")) {
                    ctx_.backups.restore(ctx_.user(), name);
                    ctx_.ui.success("Snapshot restored.");
                } else {
                    ctx_.ui.info("Confirmation text did not match - nothing restored.");
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

void AdminScreens::auditLog() {
    ctx_.screen({"Administration", "Audit Log"});
    AccessPolicy::require(ctx_.user(), Permission::ViewAuditLog, ctx_.audit);
    const auto v = ctx_.audit.verify();
    if (v.ok) ctx_.ui.success("Hash chain verified: " + std::to_string(v.lines) + " entries, no tampering detected.");
    else ctx_.ui.error("TAMPERING DETECTED: the audit chain breaks at line " + std::to_string(v.firstBadLine) + ".");
    ctx_.audit.info("AUDIT_VIEWED", ctx_.user().username, "");
    const auto lines = ctx_.audit.tail(25);
    ctx_.ui.heading("Most recent entries");
    for (const auto& raw : lines) {
        std::string line = raw;
        const auto p = line.rfind(" | chain:");
        if (p != std::string::npos) line.resize(p);
        ctx_.ui.line(" " + text::truncate(line, ctx_.ui.contentWidth() - 2, ctx_.term.unicodeEnabled() ? "\xE2\x80\xA6" : "~"));
    }
    ctx_.ui.hint("Full file: " + ctx_.audit.file().string());
    ctx_.in.pause();
}

void AdminScreens::importLegacy() {
    ctx_.screen({"Administration", "Import Legacy Data"});
    ctx_.ui.hint("Imports STUDENT/COURSE blocks from the original program's students.dat.");
    ctx_.ui.hint("Letter grades become mid-band percentages (GPA preserved); a backup is taken first.");
    const std::string path = ctx_.in.line("Path to students.dat", 255, false, nullptr, "students.dat");
    if (!ctx_.in.confirm("Import from '" + path + "' into the live database?", false)) {
        ctx_.ui.info("Cancelled - nothing imported.");
        return;
    }
    const ImportReport r = ctx_.legacy.import(ctx_.user(), path);
    ctx_.ui.keyValues("Import report", {{"Students imported", std::to_string(r.studentsImported)},
                                        {"Students skipped", std::to_string(r.studentsSkipped)},
                                        {"Courses created", std::to_string(r.coursesCreated)},
                                        {"Enrollments imported", std::to_string(r.enrollmentsImported)},
                                        {"Warnings", std::to_string(r.warnings.size())}});
    const std::size_t shown = std::min<std::size_t>(r.warnings.size(), 15);
    for (std::size_t i = 0; i < shown; ++i) ctx_.ui.warning(r.warnings[i]);
    if (r.warnings.size() > shown) ctx_.ui.hint("... and " + std::to_string(r.warnings.size() - shown) + " more.");
    ctx_.in.pause();
}

void AdminScreens::systemInfo() {
    ctx_.screen({"Administration", "System Information"});
    const Database& db = ctx_.store.db();
    ctx_.ui.keyValues("System information",
                      {{"Database file", ctx_.store.repository().file().string()},
                       {"Backup directory", ctx_.backups.directory().string()},
                       {"Export directory", ctx_.exports.directory().string()},
                       {"Audit log", ctx_.audit.file().string()},
                       {"Audit log health", ctx_.audit.healthy() ? "OK" : "WRITE ERRORS"},
                       {"Users", std::to_string(db.users.size())},
                       {"Students", std::to_string(db.students.size())},
                       {"Courses", std::to_string(db.courses.size())},
                       {"Enrollments", std::to_string(db.enrollments.size())},
                       {"Terminal", std::to_string(ctx_.term.width()) + "x" + std::to_string(ctx_.term.height()) +
                                        (ctx_.term.colorEnabled() ? ", colour" : ", no colour") +
                                        (ctx_.term.unicodeEnabled() ? ", Unicode" : ", ASCII")}});
    ctx_.in.pause();
}

}  // namespace sms::ui
