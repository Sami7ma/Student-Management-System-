#pragma once
#include <optional>
#include <string>

namespace sms {

struct Student {
    std::string id;          // e.g. "NSE/1001/16" (unique, upper-case)
    std::string name;
    std::string email;       // optional
    std::string phone;       // optional, normalised (digits with optional leading '+')
    std::string department;
    std::optional<std::string> dateOfBirth;  // YYYY-MM-DD, optional
    std::string enrolledOn;  // YYYY-MM-DD
    bool active = true;      // false == soft-deleted
    std::string deletedAt;   // timestamp when soft-deleted
};

}  // namespace sms
