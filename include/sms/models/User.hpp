#pragma once
#include <optional>
#include <string>
#include <string_view>

namespace sms {

enum class Role { Admin, Faculty, Student };

std::string_view toString(Role role);
std::optional<Role> parseRole(std::string_view text);

/// A login account. `passwordHash` is "pbkdf2-sha256$<iterations>$<saltHex>$<hashHex>"; the
/// plaintext password is never stored anywhere.
struct User {
    std::string username;
    Role role = Role::Student;
    std::string passwordHash;
    std::string linkedStudentId;  // required for Role::Student, empty otherwise
    bool active = true;
    bool mustChangePassword = false;
    std::string createdAt;
};

}  // namespace sms
