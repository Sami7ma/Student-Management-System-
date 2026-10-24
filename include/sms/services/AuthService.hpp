#pragma once
// Authentication and account management.
//  * Passwords: PBKDF2-HMAC-SHA-256, 16-byte random salt per user, 100 000 iterations.
//    Stored as "pbkdf2-sha256$<iterations>$<saltHex>$<hashHex>"; verified in constant time.
//  * Brute-force protection: 5 consecutive failures lock the username for 5 minutes.
//  * Unknown usernames cost the same CPU time as wrong passwords (no user enumeration).
#include <map>
#include <string>
#include <vector>

#include "sms/models/User.hpp"
#include "sms/storage/DataStore.hpp"
#include "sms/storage/Logger.hpp"

namespace sms {

class AuthService {
public:
    static constexpr std::uint32_t kDefaultIterations = 100000;
    static constexpr int kMaxFailures = 5;
    static constexpr std::int64_t kLockSeconds = 300;

    AuthService(storage::DataStore& store, storage::AuditLogger& audit) : store_(store), audit_(audit) {}

    static std::string hashPassword(std::string_view password, std::uint32_t iterations = kDefaultIterations);
    static bool verifyPassword(std::string_view password, std::string_view storedHash);

    bool needsBootstrap() const { return store_.db().users.empty(); }
    /// First-run only: creates the initial administrator chosen by the operator.
    User bootstrapAdmin(std::string_view username, std::string_view password);

    User login(std::string_view username, std::string_view password);
    void logout(const User& user);
    User refresh(const User& user) const;  // latest stored copy; throws AuthException if gone/disabled

    void changePassword(const User& actor, std::string_view oldPassword, std::string_view newPassword);
    void createUser(const User& actor, std::string_view username, Role role, std::string_view password,
                    std::string_view linkedStudentId);
    void resetPassword(const User& actor, std::string_view username, std::string_view newPassword);
    void setActive(const User& actor, std::string_view username, bool active);
    std::vector<User> listUsers(const User& actor) const;

private:
    struct Attempt {
        int failures = 0;
        std::int64_t lockedUntil = 0;
    };
    void recordFailure(const std::string& key);

    storage::DataStore& store_;
    storage::AuditLogger& audit_;
    std::map<std::string, Attempt> attempts_;
};

}  // namespace sms
