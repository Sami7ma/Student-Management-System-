#include "sms/models/User.hpp"

#include "sms/core/Text.hpp"

namespace sms {

std::string_view toString(Role role) {
    switch (role) {
        case Role::Admin: return "Admin";
        case Role::Faculty: return "Faculty";
        case Role::Student: return "Student";
    }
    return "Unknown";
}

std::optional<Role> parseRole(std::string_view text) {
    const std::string t = text::toLower(text::trim(text));
    if (t == "admin") return Role::Admin;
    if (t == "faculty" || t == "staff") return Role::Faculty;
    if (t == "student") return Role::Student;
    return std::nullopt;
}

}  // namespace sms
