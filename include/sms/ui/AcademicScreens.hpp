#pragma once
#include "sms/ui/AppContext.hpp"

namespace sms::ui {

/// Enrollments & grades.
class EnrollmentScreens {
public:
    explicit EnrollmentScreens(AppContext& ctx) : ctx_(ctx) {}
    void menu(std::function<void(const std::function<void()>&)> guarded);

private:
    Enrollment pick(const std::string& studentId, bool gradedOnly);
    void enroll();
    void recordGrade();
    void clearGrade();
    void remove();
    AppContext& ctx_;
};

/// Analytics dashboard, statistics, transcripts and exports.
class AnalyticsScreens {
public:
    explicit AnalyticsScreens(AppContext& ctx) : ctx_(ctx) {}
    void menu(std::function<void(const std::function<void()>&)> guarded);
    void reports(std::function<void(const std::function<void()>&)> guarded);
    void transcript(const std::string& studentId);   // view + optional export

private:
    void overview();
    void courseStats();
    void departmentStats();
    void topStudents();
    AppContext& ctx_;
};

}  // namespace sms::ui
