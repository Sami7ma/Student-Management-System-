#pragma once
// Everything a screen needs, bundled so screens stay free of global state.
#include <optional>
#include <string>
#include <vector>

#include <algorithm>
#include <functional>

#include "sms/core/Text.hpp"
#include "sms/services/AccessPolicy.hpp"
#include "sms/services/AnalyticsEngine.hpp"
#include "sms/services/AuthService.hpp"
#include "sms/services/BackupService.hpp"
#include "sms/services/CourseManager.hpp"
#include "sms/services/EnrollmentManager.hpp"
#include "sms/services/ExportService.hpp"
#include "sms/services/LegacyImporter.hpp"
#include "sms/services/StudentManager.hpp"
#include "sms/ui/InputValidator.hpp"
#include "sms/ui/TerminalRenderer.hpp"

namespace sms::ui {

struct AppContext {
    Terminal& term;
    TerminalRenderer& ui;
    InputValidator& in;
    storage::DataStore& store;
    storage::AuditLogger& audit;
    AuthService& auth;
    StudentManager& students;
    CourseManager& courses;
    EnrollmentManager& enrollments;
    AnalyticsEngine& analytics;
    ExportService& exports;
    BackupService& backups;
    LegacyImporter& legacy;

    std::optional<User> session;
    std::string lastTerm;  // remembered between enrollment forms

    const User& user() const { return *session; }

    StatusInfo status() const {
        StatusInfo s;
        if (session) {
            s.user = session->username;
            s.role = std::string(toString(session->role));
        }
        s.auditHealthy = audit.healthy();
        s.dataFile = store.repository().file().filename().string();
        return s;
    }

    /// Starts a screen: clear, banner, status bar and "Dashboard > ..." breadcrumbs.
    void screen(std::vector<std::string> crumbs) {
        crumbs.insert(crumbs.begin(), "Dashboard");
        const StatusInfo st = status();
        ui.beginScreen(crumbs, session ? &st : nullptr);
    }
};

/// Paginated navigation. Handles n/p/g itself and hands every other command back to the caller.
class Pager {
public:
    Pager(AppContext& ctx, std::size_t pageSize) : ctx_(ctx), pageSize_(pageSize == 0 ? 10 : pageSize) {}
    void reset() { page_ = 0; }
    std::size_t pageSize() const { return pageSize_; }

    template <typename RenderFn>
    std::string run(std::size_t totalItems, RenderFn&& render, const std::string& extraHints) {
        while (true) {
            const std::size_t pages = totalItems == 0 ? 1 : (totalItems + pageSize_ - 1) / pageSize_;
            if (page_ >= pages) page_ = pages - 1;
            const std::size_t begin = page_ * pageSize_;
            const std::size_t end = std::min(totalItems, begin + pageSize_);
            render(begin, end, page_ + 1, pages);
            ctx_.ui.hint("[N]ext  [P]rev  [G]o to page  " + extraHints + "  [Q]uit");
            const std::string cmd = ctx_.in.menuKey("Command");
            if (cmd == "n" || cmd == "next") {
                if (page_ + 1 < pages) ++page_;
                else ctx_.ui.fieldError("Already on the last page.");
            } else if (cmd == "p" || cmd == "prev") {
                if (page_ > 0) --page_;
                else ctx_.ui.fieldError("Already on the first page.");
            } else if (cmd == "g" || cmd == "go") {
                page_ = static_cast<std::size_t>(ctx_.in.integer("Page number", 1, static_cast<int>(pages))) - 1;
            } else {
                return cmd;
            }
        }
    }

private:
    AppContext& ctx_;
    std::size_t pageSize_;
    std::size_t page_ = 0;
};

inline std::string fmtNum(double v, int decimals = 2) { return text::formatFixed(v, decimals); }

inline Style standingStyle(Standing s) {
    switch (s) {
        case Standing::DeansList: return Style::Gold;
        case Standing::GoodStanding: return Style::Green;
        case Standing::Probation: return Style::Red;
        case Standing::NoGrades: return Style::Gray;
    }
    return Style::Reset;
}

}  // namespace sms::ui
