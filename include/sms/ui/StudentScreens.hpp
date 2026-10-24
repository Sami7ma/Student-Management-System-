#pragma once
#include "sms/ui/AppContext.hpp"

namespace sms::ui {

class StudentScreens {
public:
    explicit StudentScreens(AppContext& ctx) : ctx_(ctx) {}
    void menu(std::function<void(const std::function<void()>&)> guarded);

    void browse(SearchCriteria criteria, SortSpec sort, const std::string& title);
    void search();
    void viewProfile();
    void add();
    void edit();
    void deactivate();
    void recycleBin();

private:
    SortSpec askSort();
    AppContext& ctx_;
};

}  // namespace sms::ui
