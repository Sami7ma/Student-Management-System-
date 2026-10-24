#pragma once
#include "sms/ui/AppContext.hpp"

namespace sms::ui {

class CourseScreens {
public:
    explicit CourseScreens(AppContext& ctx) : ctx_(ctx) {}
    void menu(std::function<void(const std::function<void()>&)> guarded);

    void list();
    void add();
    void edit();
    void toggleActive();
    void purge();

private:
    AppContext& ctx_;
};

}  // namespace sms::ui
