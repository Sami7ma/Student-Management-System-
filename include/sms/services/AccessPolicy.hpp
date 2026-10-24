#pragma once
// Role-based access control. The policy is enforced inside the services (not just hidden in
// menus), so no UI path can bypass it. Every denial is written to the audit log.
#include <string_view>

#include "sms/models/User.hpp"
#include "sms/storage/Logger.hpp"

namespace sms {

enum class Permission {
    ViewAnyStudent,
    ViewOwnRecord,
    CreateStudent,
    UpdateStudent,
    SoftDeleteStudent,
    RestoreStudent,
    HardDeleteStudent,
    ManageCourses,        // create / edit / retire / reactivate courses
    HardDeleteCourse,
    ManageEnrollments,
    RecordGrades,
    ViewAnalytics,
    ExportData,
    ExportOwnTranscript,
    ManageBackups,
    ManageUsers,
    ViewAuditLog,
    ImportLegacy,
    ChangeOwnPassword
};

class AccessPolicy {
public:
    static bool allows(Role role, Permission permission);
    static std::string_view describe(Permission permission);
    /// Throws AuthException (and audits ACCESS_DENIED) when the actor lacks the permission
    /// or the account is disabled.
    static void require(const User& actor, Permission permission, storage::AuditLogger& audit);
    /// Admin/Faculty may touch any student; a Student account only its own linked record.
    static void requireStudentAccess(const User& actor, std::string_view studentId, storage::AuditLogger& audit);
};

}  // namespace sms
