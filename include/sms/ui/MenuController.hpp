#pragma once
// Top-level flow: first-run setup, login, forced password change, role-specific dashboard,
// idle-timeout and uniform exception-to-message translation for every action.
#include <functional>

#include "sms/ui/AppContext.hpp"

namespace sms::ui {

class MenuController {
public:
    static constexpr std::int64_t kIdleTimeoutSeconds = 600;

    explicit MenuController(AppContext& ctx) : ctx_(ctx) {}
    /// Runs until the user exits. Returns the process exit code.
    int run();

private:
    void firstRunSetup();
    bool login();                     // false => user chose to quit
    void forcePasswordChange();
    void staffDashboard();
    void studentDashboard();
    void changePassword();
    void guarded(const std::function<void()>& action);
    void logout(const std::string& why);
    void showHomeSummary();

    AppContext& ctx_;
    bool sessionExpired_ = false;
};

}  // namespace sms::ui
