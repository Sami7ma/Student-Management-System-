#include "sms/services/AuthService.hpp"

#include <algorithm>

#include "sms/core/Crypto.hpp"
#include "sms/core/Exceptions.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Rules.hpp"
#include "sms/services/AccessPolicy.hpp"

namespace sms {

namespace {
constexpr std::string_view kAlgorithm = "pbkdf2-sha256";
constexpr std::size_t kSaltBytes = 16;
constexpr std::size_t kHashBytes = 32;

const std::string& dummyHash() {
    static const std::string h = AuthService::hashPassword("dummy-password-for-timing-1A", AuthService::kDefaultIterations);
    return h;
}
}  // namespace

std::string AuthService::hashPassword(std::string_view password, std::uint32_t iterations) {
    const crypto::Bytes salt = crypto::randomBytes(kSaltBytes);
    const crypto::Bytes derived = crypto::pbkdf2HmacSha256(password, salt, iterations, kHashBytes);
    return std::string(kAlgorithm) + "$" + std::to_string(iterations) + "$" + crypto::toHex(salt) + "$" +
           crypto::toHex(derived);
}

bool AuthService::verifyPassword(std::string_view password, std::string_view storedHash) {
    const auto parts = text::split(storedHash, '$');
    if (parts.size() != 4 || parts[0] != kAlgorithm) return false;
    const auto iterations = text::parseInteger(parts[1]);
    if (!iterations || *iterations < 1 || *iterations > 5000000) return false;
    const auto salt = crypto::fromHex(parts[2]);
    const auto expected = crypto::fromHex(parts[3]);
    if (!salt || !expected || expected->empty()) return false;
    const crypto::Bytes actual =
        crypto::pbkdf2HmacSha256(password, *salt, static_cast<std::uint32_t>(*iterations), expected->size());
    return crypto::constantTimeEquals(actual, *expected);
}

User AuthService::bootstrapAdmin(std::string_view usernameRaw, std::string_view password) {
    if (!needsBootstrap()) throw AuthException("Setup already completed.");
    const std::string username = rules::normalizeUsername(usernameRaw);
    if (auto e = rules::checkUsername(username)) throw ValidationException(*e);
    if (auto e = rules::checkPassword(password)) throw ValidationException(*e);
    User admin;
    admin.username = username;
    admin.role = Role::Admin;
    admin.passwordHash = hashPassword(password);
    admin.createdAt = timeutil::nowTimestamp();
    store_.transact([&](Database& db) { db.users.push_back(admin); });
    audit_.critical("BOOTSTRAP_ADMIN", username, "initial administrator account created");
    return admin;
}

void AuthService::recordFailure(const std::string& key) {
    Attempt& a = attempts_[key];
    ++a.failures;
    if (a.failures >= kMaxFailures) {
        a.lockedUntil = timeutil::epochSeconds() + kLockSeconds;
        a.failures = 0;
    }
}

User AuthService::login(std::string_view usernameRaw, std::string_view password) {
    const std::string username = rules::normalizeUsername(usernameRaw);
    const std::string key = text::sanitizeForLog(username, 40);

    Attempt& attempt = attempts_[username];
    const std::int64_t now = timeutil::epochSeconds();
    if (attempt.lockedUntil > now) {
        audit_.warn("LOGIN_LOCKED", key, "login attempt while locked out");
        throw AuthException("Too many failed attempts. Try again in " + std::to_string(attempt.lockedUntil - now) +
                            " seconds.");
    }

    const User* found = store_.db().findUser(username);
    const bool formatOk = !rules::checkUsername(username).has_value();
    const std::string& hash = (found != nullptr && formatOk) ? found->passwordHash : dummyHash();
    const bool passwordOk = verifyPassword(password, hash);

    if (found == nullptr || !formatOk || !passwordOk) {
        recordFailure(username);
        audit_.warn("LOGIN_FAILED", key, found == nullptr ? "unknown username" : "wrong password");
        throw AuthException("Invalid username or password.");
    }
    if (!found->active) {
        audit_.warn("LOGIN_DENIED", key, "account disabled");
        throw AuthException("Invalid username or password.");
    }
    attempts_.erase(username);
    audit_.info("LOGIN_OK", found->username, "role " + std::string(toString(found->role)));
    return *found;
}

void AuthService::logout(const User& user) { audit_.info("LOGOUT", user.username, ""); }

User AuthService::refresh(const User& user) const {
    const User* u = store_.db().findUser(user.username);
    if (u == nullptr || !u->active) throw AuthException("Your account is no longer available.");
    return *u;
}

void AuthService::changePassword(const User& actor, std::string_view oldPassword, std::string_view newPassword) {
    AccessPolicy::require(actor, Permission::ChangeOwnPassword, audit_);
    const User* stored = store_.db().findUser(actor.username);
    if (stored == nullptr) throw NotFoundException("Account not found.");
    if (!verifyPassword(oldPassword, stored->passwordHash)) {
        audit_.warn("PASSWORD_CHANGE_FAILED", actor.username, "current password incorrect");
        throw AuthException("Current password is incorrect.");
    }
    if (oldPassword == newPassword) throw ValidationException("New password must differ from the current one.");
    if (auto e = rules::checkPassword(newPassword)) throw ValidationException(*e);
    const std::string newHash = hashPassword(newPassword);
    store_.transact([&](Database& db) {
        User* u = db.findUser(actor.username);
        u->passwordHash = newHash;
        u->mustChangePassword = false;
    });
    audit_.info("PASSWORD_CHANGED", actor.username, "own password");
}

void AuthService::createUser(const User& actor, std::string_view usernameRaw, Role role, std::string_view password,
                             std::string_view linkedStudentId) {
    AccessPolicy::require(actor, Permission::ManageUsers, audit_);
    const std::string username = rules::normalizeUsername(usernameRaw);
    if (auto e = rules::checkUsername(username)) throw ValidationException(*e);
    if (auto e = rules::checkPassword(password)) throw ValidationException(*e);
    const std::string studentId = rules::normalizeStudentId(linkedStudentId);
    User u;
    u.username = username;
    u.role = role;
    u.passwordHash = hashPassword(password);
    u.mustChangePassword = true;
    u.createdAt = timeutil::nowTimestamp();
    if (role == Role::Student) {
        if (auto e = rules::checkStudentId(studentId)) throw ValidationException(*e);
        u.linkedStudentId = studentId;
    }
    store_.transact([&](Database& db) {
        if (db.findUser(username) != nullptr) throw ValidationException("Username '" + username + "' already exists.");
        if (role == Role::Student) {
            const Student* s = db.findStudent(studentId);
            if (s == nullptr || !s->active) throw NotFoundException("No active student with ID " + studentId + ".");
            const bool taken = std::any_of(db.users.begin(), db.users.end(),
                                           [&](const User& x) { return x.linkedStudentId == studentId; });
            if (taken) throw ValidationException("That student already has a login account.");
        }
        db.users.push_back(u);
    });
    audit_.critical("USER_CREATED", actor.username,
                    "username=" + username + " role=" + std::string(toString(role)));
}

void AuthService::resetPassword(const User& actor, std::string_view usernameRaw, std::string_view newPassword) {
    AccessPolicy::require(actor, Permission::ManageUsers, audit_);
    const std::string username = rules::normalizeUsername(usernameRaw);
    if (auto e = rules::checkPassword(newPassword)) throw ValidationException(*e);
    const std::string newHash = hashPassword(newPassword);
    store_.transact([&](Database& db) {
        User* u = db.findUser(username);
        if (u == nullptr) throw NotFoundException("User '" + username + "' not found.");
        u->passwordHash = newHash;
        u->mustChangePassword = true;
    });
    attempts_.erase(username);
    audit_.critical("PASSWORD_RESET", actor.username, "target=" + username);
}

void AuthService::setActive(const User& actor, std::string_view usernameRaw, bool active) {
    AccessPolicy::require(actor, Permission::ManageUsers, audit_);
    const std::string username = rules::normalizeUsername(usernameRaw);
    if (!active && username == actor.username) throw ValidationException("You cannot disable your own account.");
    store_.transact([&](Database& db) {
        User* u = db.findUser(username);
        if (u == nullptr) throw NotFoundException("User '" + username + "' not found.");
        if (!active && u->role == Role::Admin) {
            const auto activeAdmins = std::count_if(db.users.begin(), db.users.end(), [](const User& x) {
                return x.role == Role::Admin && x.active;
            });
            if (activeAdmins <= 1) throw ValidationException("Cannot disable the last active administrator.");
        }
        u->active = active;
    });
    audit_.critical(active ? "USER_ENABLED" : "USER_DISABLED", actor.username, "target=" + username);
}

std::vector<User> AuthService::listUsers(const User& actor) const {
    AccessPolicy::require(actor, Permission::ManageUsers, audit_);
    std::vector<User> users = store_.db().users;
    std::sort(users.begin(), users.end(), [](const User& a, const User& b) { return a.username < b.username; });
    return users;
}

}  // namespace sms
