#pragma once
// Domain validation rules shared by the UI (immediate feedback), the services (authoritative
// enforcement) and the storage layer (schema validation on startup).
// Every check* function returns std::nullopt when the value is valid, otherwise a
// human-readable error message.
#include <optional>
#include <string>
#include <string_view>

#include "sms/models/Course.hpp"
#include "sms/models/Enrollment.hpp"
#include "sms/models/Student.hpp"
#include "sms/models/User.hpp"

namespace sms::rules {

using Error = std::optional<std::string>;

constexpr std::size_t kMaxNameLen = 100;
constexpr std::size_t kMaxEmailLen = 254;
constexpr std::size_t kMaxDepartmentLen = 60;
constexpr std::size_t kMaxTitleLen = 100;
constexpr std::size_t kMaxPasswordLen = 64;
constexpr std::size_t kMinPasswordLen = 8;
constexpr int kMinStudentAge = 15;
constexpr int kMaxStudentAge = 100;
constexpr double kMinCredits = 0.5, kMaxCredits = 12.0;
constexpr double kMinEcts = 0.0, kMaxEcts = 30.0;
constexpr double kMinGpa = 0.0, kMaxGpa = 4.0;

// Normalisers (apply before checking / storing)
std::string normalizeStudentId(std::string_view raw);
std::string normalizeCourseCode(std::string_view raw);
std::string normalizePhone(std::string_view raw);
std::string normalizeUsername(std::string_view raw);
std::string collapseSpaces(std::string_view raw);

// Field checks
Error checkStudentId(std::string_view id);            // AAA/1234/16
Error checkPersonName(std::string_view name);
Error checkEmail(std::string_view email);             // non-empty email
Error checkPhone(std::string_view normalizedPhone);   // non-empty phone, 7-15 digits
Error checkDepartment(std::string_view dept);
Error checkCourseCode(std::string_view code);         // LETTERS + DIGITS, e.g. COSC1011
Error checkCourseTitle(std::string_view title);
Error checkCredits(double credits);
Error checkEcts(double ects);
Error checkMark(double mark);
Error checkTerm(std::string_view term);
Error checkDate(std::string_view date);
Error checkDateOfBirth(std::string_view date, std::string_view today);
Error checkEnrollmentDate(std::string_view date, std::string_view today);
Error checkUsername(std::string_view username);
Error checkPassword(std::string_view password);
Error checkGpaRange(double lo, double hi);
Error checkFreeText(std::string_view text, std::size_t maxLen, std::string_view label);

// Whole-entity checks (used by services and by schema validation)
Error validateStudent(const Student& s, std::string_view today);
Error validateCourse(const Course& c);
Error validateEnrollment(const Enrollment& e);
Error validateUser(const User& u);

}  // namespace sms::rules
