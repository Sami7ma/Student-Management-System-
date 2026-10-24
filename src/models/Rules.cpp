#include "sms/models/Rules.hpp"

#include <algorithm>
#include <cmath>

#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"

namespace sms::rules {

namespace {

bool isAsciiAlpha(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
bool isAsciiDigit(char c) { return c >= '0' && c <= '9'; }

Error checkText(std::string_view v, std::string_view label, std::size_t maxLen) {
    if (v.empty()) return std::string(label) + " must not be empty.";
    if (v.size() > maxLen) return std::string(label) + " is too long (max " + std::to_string(maxLen) + " bytes).";
    if (!text::isValidUtf8(v)) return std::string(label) + " contains invalid UTF-8.";
    if (text::hasControlChars(v)) return std::string(label) + " contains control characters.";
    if (v.front() == ' ' || v.back() == ' ') return std::string(label) + " must not start or end with spaces.";
    return std::nullopt;
}

}  // namespace

std::string collapseSpaces(std::string_view raw) {
    std::string out;
    bool lastSpace = true;
    for (char c : raw) {
        const bool space = (c == ' ' || c == '\t');
        if (space) {
            if (!lastSpace) out.push_back(' ');
            lastSpace = true;
        } else {
            out.push_back(c);
            lastSpace = false;
        }
    }
    if (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

std::string normalizeStudentId(std::string_view raw) { return text::toUpper(text::trim(raw)); }
std::string normalizeCourseCode(std::string_view raw) {
    std::string t = text::toUpper(text::trim(raw));
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    return t;
}
std::string normalizeUsername(std::string_view raw) { return text::toLower(text::trim(raw)); }

std::string normalizePhone(std::string_view raw) {
    std::string out;
    for (char c : raw) {
        if (c == ' ' || c == '-' || c == '(' || c == ')' || c == '.' || c == '\t') continue;
        out.push_back(c);
    }
    return out;
}

Error checkStudentId(std::string_view id) {
    // Format: 2-5 letters / 3-6 digits / 2 digits (year), e.g. NSE/1001/16. Checked by hand
    // rather than with std::regex so an attacker-controlled string can never trigger deep recursion.
    if (id.empty()) return "Student ID must not be empty.";
    if (id.size() > 20) return "Student ID is too long.";
    const auto parts = text::split(id, '/');
    const bool ok = parts.size() == 3 && parts[0].size() >= 2 && parts[0].size() <= 5 &&
                    std::all_of(parts[0].begin(), parts[0].end(), [](char c) { return c >= 'A' && c <= 'Z'; }) &&
                    parts[1].size() >= 3 && parts[1].size() <= 6 &&
                    std::all_of(parts[1].begin(), parts[1].end(), isAsciiDigit) && parts[2].size() == 2 &&
                    std::all_of(parts[2].begin(), parts[2].end(), isAsciiDigit);
    if (!ok) return "Student ID must look like NSE/1001/16 (2-5 letters / 3-6 digits / 2-digit year).";
    return std::nullopt;
}

Error checkPersonName(std::string_view name) {
    if (auto e = checkText(name, "Name", kMaxNameLen)) return e;
    if (name.size() < 2) return "Name is too short.";
    bool hasLetter = false;
    for (char c : name) {
        const auto u = static_cast<unsigned char>(c);
        if (isAsciiAlpha(c) || u >= 0x80) {
            hasLetter = true;
        } else if (c != ' ' && c != '\'' && c != '-' && c != '.') {
            return "Name may only contain letters, spaces, apostrophes, hyphens and periods.";
        }
    }
    if (!hasLetter) return "Name must contain letters.";
    return std::nullopt;
}

Error checkEmail(std::string_view email) {
    // Grammar (equivalent to the regex ^[A-Za-z0-9._%+-]{1,64}@([A-Za-z0-9-]{1,63}\.)+[A-Za-z]{2,24}$
    // with stricter label rules). Hand-written instead of std::regex: linear time, no recursion
    // on attacker-controlled input, and no <regex> warnings under -Werror on some GCC versions.
    if (email.empty()) return "Email must not be empty.";
    if (email.size() > kMaxEmailLen) return "Email is too long (max 254 characters).";
    const std::string kMalformed = "Email address is malformed (expected name@domain.tld).";
    const std::size_t at = email.find('@');
    if (at == std::string_view::npos || email.find('@', at + 1) != std::string_view::npos) return kMalformed;
    const std::string_view local = email.substr(0, at);
    const std::string_view domain = email.substr(at + 1);
    if (local.empty() || local.size() > 64 || local.front() == '.' || local.back() == '.' ||
        local.find("..") != std::string_view::npos) {
        return kMalformed;
    }
    for (char c : local) {
        if (!(isAsciiAlpha(c) || isAsciiDigit(c) || c == '.' || c == '_' || c == '%' || c == '+' || c == '-')) {
            return kMalformed;
        }
    }
    const auto labels = text::split(domain, '.');
    if (labels.size() < 2) return kMalformed;
    for (std::size_t i = 0; i + 1 < labels.size(); ++i) {
        const std::string& l = labels[i];
        if (l.empty() || l.size() > 63 || l.front() == '-' || l.back() == '-') return kMalformed;
        for (char c : l) {
            if (!(isAsciiAlpha(c) || isAsciiDigit(c) || c == '-')) return kMalformed;
        }
    }
    const std::string& tld = labels.back();
    if (tld.size() < 2 || tld.size() > 24 || !std::all_of(tld.begin(), tld.end(), isAsciiAlpha)) return kMalformed;
    return std::nullopt;
}

Error checkPhone(std::string_view phone) {
    if (phone.empty()) return "Phone number must not be empty.";
    std::size_t start = (phone.front() == '+') ? 1 : 0;
    const std::size_t digits = phone.size() - start;
    if (digits < 7 || digits > 15) return "Phone number must have 7-15 digits (optional leading '+').";
    for (std::size_t i = start; i < phone.size(); ++i) {
        if (!isAsciiDigit(phone[i])) return "Phone number may contain only digits, spaces, dashes and a leading '+'.";
    }
    return std::nullopt;
}

Error checkDepartment(std::string_view dept) {
    if (auto e = checkText(dept, "Department", kMaxDepartmentLen)) return e;
    if (dept.size() < 2) return "Department is too short.";
    for (char c : dept) {
        const auto u = static_cast<unsigned char>(c);
        if (!(isAsciiAlpha(c) || isAsciiDigit(c) || u >= 0x80 || c == ' ' || c == '&' || c == '-' ||
              c == '\'' || c == '(' || c == ')' || c == ',' || c == '.' || c == '/')) {
            return "Department contains unsupported characters.";
        }
    }
    return std::nullopt;
}

Error checkCourseCode(std::string_view code) {
    if (code.empty()) return "Course code must not be empty.";
    if (code.size() > 12) return "Course code is too long.";
    std::size_t i = 0;
    while (i < code.size() && code[i] >= 'A' && code[i] <= 'Z') ++i;
    const std::size_t letters = i;
    while (i < code.size() && isAsciiDigit(code[i])) ++i;
    const std::size_t digits = i - letters;
    if (i != code.size() || letters < 2 || letters > 6 || digits < 3 || digits > 5) {
        return "Course code must be 2-6 letters followed by 3-5 digits (e.g. COSC1011).";
    }
    return std::nullopt;
}

Error checkCourseTitle(std::string_view title) {
    if (auto e = checkText(title, "Course title", kMaxTitleLen)) return e;
    return std::nullopt;
}

Error checkCredits(double credits) {
    if (!std::isfinite(credits) || credits < kMinCredits || credits > kMaxCredits) {
        return "Credit hours must be between 0.5 and 12.";
    }
    return std::nullopt;
}

Error checkEcts(double ects) {
    if (!std::isfinite(ects) || ects < kMinEcts || ects > kMaxEcts) return "ECTS must be between 0 and 30.";
    return std::nullopt;
}

Error checkMark(double mark) {
    if (!std::isfinite(mark) || mark < 0.0 || mark > 100.0) return "Mark must be between 0 and 100.";
    return std::nullopt;
}

Error checkTerm(std::string_view term) {
    if (term.size() < 2 || term.size() > 16) return "Term must be 2-16 characters (e.g. 2025-S1).";
    for (char c : term) {
        if (!(isAsciiAlpha(c) || isAsciiDigit(c) || c == '-')) {
            return "Term may contain only letters, digits and '-'.";
        }
    }
    return std::nullopt;
}

Error checkDate(std::string_view date) {
    if (!timeutil::isValidDate(date)) return "Date must be a real calendar date in YYYY-MM-DD format.";
    return std::nullopt;
}

Error checkDateOfBirth(std::string_view date, std::string_view today) {
    if (auto e = checkDate(date)) return e;
    if (date > today) return "Date of birth cannot be in the future.";
    const int age = timeutil::ageOn(date, today);
    if (age < kMinStudentAge || age > kMaxStudentAge) {
        return "Student age must be between " + std::to_string(kMinStudentAge) + " and " +
               std::to_string(kMaxStudentAge) + ".";
    }
    return std::nullopt;
}

Error checkEnrollmentDate(std::string_view date, std::string_view today) {
    if (auto e = checkDate(date)) return e;
    if (date > today) return "Enrollment date cannot be in the future.";
    if (date < "1950-01-01") return "Enrollment date is unrealistically old.";
    return std::nullopt;
}

Error checkUsername(std::string_view username) {
    if (username.size() < 3 || username.size() > 24) return "Username must be 3-24 characters.";
    if (!(username[0] >= 'a' && username[0] <= 'z')) return "Username must start with a lowercase letter.";
    for (char c : username) {
        if (!((c >= 'a' && c <= 'z') || isAsciiDigit(c) || c == '_' || c == '.' || c == '-')) {
            return "Username may contain only a-z, 0-9, '_', '.' and '-'.";
        }
    }
    return std::nullopt;
}

Error checkPassword(std::string_view password) {
    if (password.size() < kMinPasswordLen) return "Password must be at least 8 characters.";
    if (password.size() > kMaxPasswordLen) return "Password must be at most 64 characters.";
    bool upper = false, lower = false, digit = false;
    for (char c : password) {
        if (c >= 'A' && c <= 'Z') upper = true;
        if (c >= 'a' && c <= 'z') lower = true;
        if (isAsciiDigit(c)) digit = true;
        if (static_cast<unsigned char>(c) < 0x20) return "Password contains control characters.";
    }
    if (!(upper && lower && digit)) return "Password needs an upper-case letter, a lower-case letter and a digit.";
    return std::nullopt;
}

Error checkGpaRange(double lo, double hi) {
    if (!std::isfinite(lo) || !std::isfinite(hi) || lo < kMinGpa || hi > kMaxGpa) {
        return "GPA values must be between 0.00 and 4.00.";
    }
    if (lo > hi) return "Minimum GPA cannot exceed maximum GPA.";
    return std::nullopt;
}

Error checkFreeText(std::string_view t, std::size_t maxLen, std::string_view label) {
    return checkText(t, label, maxLen);
}

Error validateStudent(const Student& s, std::string_view today) {
    if (auto e = checkStudentId(s.id)) return e;
    if (auto e = checkPersonName(s.name)) return e;
    if (!s.email.empty()) {
        if (auto e = checkEmail(s.email)) return e;
    }
    if (!s.phone.empty()) {
        if (auto e = checkPhone(s.phone)) return e;
    }
    if (auto e = checkDepartment(s.department)) return e;
    if (s.dateOfBirth) {
        if (auto e = checkDateOfBirth(*s.dateOfBirth, today)) return e;
    }
    if (auto e = checkEnrollmentDate(s.enrolledOn, today)) return e;
    return std::nullopt;
}

Error validateCourse(const Course& c) {
    if (auto e = checkCourseCode(c.code)) return e;
    if (auto e = checkCourseTitle(c.title)) return e;
    if (auto e = checkCredits(c.credits)) return e;
    if (auto e = checkEcts(c.ects)) return e;
    if (auto e = checkDepartment(c.department)) return e;
    return std::nullopt;
}

Error validateEnrollment(const Enrollment& e) {
    if (auto err = checkStudentId(e.studentId)) return err;
    if (auto err = checkCourseCode(e.courseCode)) return err;
    if (auto err = checkTerm(e.term)) return err;
    if (e.mark) {
        if (auto err = checkMark(*e.mark)) return err;
    }
    return std::nullopt;
}

Error validateUser(const User& u) {
    if (auto e = checkUsername(u.username)) return e;
    if (u.passwordHash.empty() || u.passwordHash.size() > 200) return "Stored password hash is malformed.";
    if (u.role == Role::Student) {
        if (auto e = checkStudentId(u.linkedStudentId)) return "Student account needs a valid linked student ID.";
    } else if (!u.linkedStudentId.empty()) {
        return "Only student accounts may be linked to a student record.";
    }
    return std::nullopt;
}

}  // namespace sms::rules
