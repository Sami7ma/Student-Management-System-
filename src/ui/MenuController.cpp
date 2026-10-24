#include "sms/ui/MenuController.hpp"

#include "sms/core/Exceptions.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Rules.hpp"
#include "sms/ui/AcademicScreens.hpp"
#include "sms/ui/AdminScreens.hpp"
#include "sms/ui/CourseScreens.hpp"
#include "sms/ui/Prompts.hpp"
#include "sms/ui/StudentScreens.hpp"

namespace sms::ui {

void MenuController::guarded(const std::function<void()>& action) {
    try {
        action();
    } catch (const OperationCancelled&) {
        ctx_.ui.info("Cancelled - nothing was changed.");
    } catch (const InputClosedException&) {
        throw;  // stdin is gone: let run() shut down cleanly
    } catch (const ValidationException& e) {
        ctx_.ui.warning(e.what());
    } catch (const NotFoundException& e) {
        ctx_.ui.warning(e.what());
    } catch (const AuthException& e) {
        ctx_.ui.error(e.what());
    } catch (const DatabaseException& e) {
        ctx_.ui.error(std::string("Storage problem: ") + e.what());
        ctx_.audit.critical("DATABASE_ERROR", ctx_.session ? ctx_.session->username : "-", e.what());
    } catch (const std::exception& e) {
        ctx_.ui.error(std::string("Unexpected error: ") + e.what());
        ctx_.audit.critical("UNEXPECTED_ERROR", ctx_.session ? ctx_.session->username : "-", e.what());
    }
    ctx_.in.pause();
}

void MenuController::logout(const std::string& why) {
    if (ctx_.session) {
        ctx_.auth.logout(*ctx_.session);
        if (!why.empty()) ctx_.audit.info("SESSION_END", ctx_.session->username, why);
    }
    ctx_.session.reset();
}

int MenuController::run() {
    try {
        if (ctx_.auth.needsBootstrap()) firstRunSetup();
        while (true) {
            if (!login()) break;
            if (ctx_.session && ctx_.session->mustChangePassword) forcePasswordChange();
            if (!ctx_.session) continue;
            sessionExpired_ = false;
            if (ctx_.session->role == Role::Student) studentDashboard();
            else staffDashboard();
            if (sessionExpired_) {
                logout("idle timeout");
                ctx_.ui.warning("Session expired after " + std::to_string(kIdleTimeoutSeconds / 60) + " minutes of inactivity. Please sign in again.");
            } else {
                logout("");
                ctx_.ui.info("Signed out.");
            }
        }
    } catch (const InputClosedException&) {
        ctx_.ui.out() << "\n";
        ctx_.ui.info("Input closed - exiting.");
        if (ctx_.session) logout("input closed");
    } catch (const OperationCancelled&) {
        ctx_.ui.out() << "\n";
        if (ctx_.session) logout("cancelled");
    }
    ctx_.ui.out() << "\n Goodbye.\n";
    return 0;
}

void MenuController::firstRunSetup() {
    ctx_.ui.beginScreen({"First-run setup"}, nullptr);
    ctx_.ui.info("No accounts exist yet. Create the administrator account for this installation.");
    ctx_.ui.hint("There is no default password: you choose it now. It is stored only as a salted PBKDF2-SHA-256 hash.");
    while (true) {
        try {
            const std::string name = ctx_.in.line("Administrator username", 24, false, [](const std::string& s) {
                return rules::checkUsername(rules::normalizeUsername(s));
            });
            const std::string pw = ctx_.in.newSecret("Administrator password");
            ctx_.auth.bootstrapAdmin(name, pw);
            ctx_.ui.success("Administrator '" + rules::normalizeUsername(name) + "' created. Please sign in.");
            ctx_.in.pause();
            return;
        } catch (const OperationCancelled&) {
            throw InputClosedException();  // cancelling setup quits the program
        } catch (const ValidationException& e) {
            ctx_.ui.warning(e.what());
        }
    }
}

bool MenuController::login() {
    for (int attempt = 0;; ++attempt) {
        ctx_.ui.beginScreen({"Sign in"}, nullptr);
        ctx_.ui.hint("Type /cancel as the username to exit.");
        try {
            const std::string name = ctx_.in.line("Username", 24, false);
            const std::string pw = ctx_.in.secret("Password");
            ctx_.session = ctx_.auth.login(name, pw);
            ctx_.ui.success("Welcome, " + ctx_.session->username + ".");
            return true;
        } catch (const OperationCancelled&) {
            return false;
        } catch (const AuthException& e) {
            ctx_.ui.error(e.what());
        } catch (const ValidationException& e) {
            ctx_.ui.warning(e.what());
        }
        ctx_.in.pause();
    }
}

void MenuController::forcePasswordChange() {
    ctx_.ui.beginScreen({"Password change required"}, nullptr);
    ctx_.ui.warning("Your password was set by an administrator. Choose a new one to continue.");
    while (ctx_.session && ctx_.session->mustChangePassword) {
        try {
            const std::string oldPw = ctx_.in.secret("Current password");
            const std::string newPw = ctx_.in.newSecret("New password");
            ctx_.auth.changePassword(*ctx_.session, oldPw, newPw);
            ctx_.session = ctx_.auth.refresh(*ctx_.session);
            ctx_.ui.success("Password changed.");
        } catch (const OperationCancelled&) {
            logout("password change declined");
            return;
        } catch (const AuthException& e) {
            ctx_.ui.error(e.what());
        } catch (const ValidationException& e) {
            ctx_.ui.warning(e.what());
        }
    }
}

void MenuController::changePassword() {
    ctx_.screen({"My Account", "Change Password"});
    const std::string oldPw = ctx_.in.secret("Current password");
    const std::string newPw = ctx_.in.newSecret("New password");
    ctx_.auth.changePassword(ctx_.user(), oldPw, newPw);
    ctx_.ui.success("Password changed.");
}

void MenuController::showHomeSummary() {
    if (ctx_.user().role == Role::Student) return;
    try {
        const Overview o = ctx_.analytics.overview(ctx_.user());
        ctx_.ui.out() << " " << ctx_.term.paint("Students ", Style::Gray) << o.activeStudents << "   "
                      << ctx_.term.paint("Courses ", Style::Gray) << o.activeCourses << "   "
                      << ctx_.term.paint("Enrollments ", Style::Gray) << o.enrollments << "   "
                      << ctx_.term.paint("Avg GPA ", Style::Gray) << (o.averageGpa > 0 ? fmtNum(o.averageGpa) : "n/a") << "\n";
    } catch (const SmsException&) {
        // The summary is decorative; never let it block the menu.
    }
}

void MenuController::staffDashboard() {
    StudentScreens students(ctx_);
    CourseScreens courses(ctx_);
    EnrollmentScreens enrollments(ctx_);
    AnalyticsScreens analytics(ctx_);
    AdminScreens admin(ctx_);
    auto g = [this](const std::function<void()>& f) { guarded(f); };

    while (true) {
        try {
            ctx_.session = ctx_.auth.refresh(*ctx_.session);
        } catch (const AuthException& e) {
            ctx_.ui.warning(e.what());
            ctx_.in.pause();
            return;
        }
        ctx_.screen({});
        showHomeSummary();
        const bool isAdmin = ctx_.user().role == Role::Admin;
        std::vector<MenuItem> items = {{"1", "Students", "Browse, search, add, edit, deactivate"},
                                       {"2", "Courses", "Catalogue management"},
                                       {"3", "Enrollments & Grades", "Enroll students and record marks"},
                                       {"4", "Analytics", "GPA, standing, course statistics, transcripts"},
                                       {"5", "Reports & Export", "CSV / Markdown / transcript files"}};
        if (isAdmin) items.push_back({"6", "Administration", "Users, backups, audit log, legacy import"});
        items.push_back({"7", "My account", "Change your password"});
        items.push_back({"0", "Sign out", "End this session"});
        ctx_.ui.menu("Main menu", items);

        const std::int64_t shown = timeutil::epochSeconds();
        const std::string k = ctx_.in.menuKey();
        if (timeutil::epochSeconds() - shown > kIdleTimeoutSeconds) {
            sessionExpired_ = true;
            return;
        }
        if (k == "0") return;
        if (k == "1") students.menu(g);
        else if (k == "2") courses.menu(g);
        else if (k == "3") enrollments.menu(g);
        else if (k == "4") analytics.menu(g);
        else if (k == "5") analytics.reports(g);
        else if (k == "6") {
            if (isAdmin) admin.menu(g);
            else g([&] { AccessPolicy::require(ctx_.user(), Permission::ManageUsers, ctx_.audit); });
        } else if (k == "7") g([&] { changePassword(); });
        else if (!k.empty()) ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

void MenuController::studentDashboard() {
    AnalyticsScreens analytics(ctx_);
    while (true) {
        try {
            ctx_.session = ctx_.auth.refresh(*ctx_.session);
        } catch (const AuthException& e) {
            ctx_.ui.warning(e.what());
            ctx_.in.pause();
            return;
        }
        ctx_.screen({});
        ctx_.ui.menu("Student portal", {{"1", "My profile", "Your record, enrollments and standing"},
                                        {"2", "My transcript", "Printable transcript, optional file copy"},
                                        {"3", "Change password", "Update your login password"},
                                        {"0", "Sign out", "End this session"}});
        const std::int64_t shown = timeutil::epochSeconds();
        const std::string k = ctx_.in.menuKey();
        if (timeutil::epochSeconds() - shown > kIdleTimeoutSeconds) {
            sessionExpired_ = true;
            return;
        }
        const std::string id = ctx_.user().linkedStudentId;
        if (k == "0") return;
        if (k == "1") guarded([&] { ctx_.screen({"My Profile"}); renderProfile(ctx_, id); });
        else if (k == "2") guarded([&] { analytics.transcript(id); });
        else if (k == "3") guarded([&] { changePassword(); });
        else if (!k.empty()) ctx_.ui.fieldError("Unknown option '" + text::sanitizeForLog(k, 20) + "'.");
    }
}

}  // namespace sms::ui
