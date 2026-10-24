#pragma once
#include "sms/ui/AppContext.hpp"

namespace sms::ui {

class AdminScreens {
public:
    explicit AdminScreens(AppContext& ctx) : ctx_(ctx) {}
    void menu(std::function<void(const std::function<void()>&)> guarded);

private:
    void users();
    void backups();
    void auditLog();
    void importLegacy();
    void systemInfo();
    AppContext& ctx_;
};

}  // namespace sms::ui
