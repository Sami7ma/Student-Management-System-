#include "sms/services/AccessPolicy.hpp"

#include "sms/core/Exceptions.hpp"

namespace sms {

bool AccessPolicy::allows(Role role, Permission p) {
    switch (role) {
        case Role::Admin:
            return true;
        case Role::Faculty:
            switch (p) {
                case Permission::ViewAnyStudent:
                case Permission::CreateStudent:
                case Permission::UpdateStudent:
                case Permission::ManageCourses:
                case Permission::ManageEnrollments:
                case Permission::RecordGrades:
                case Permission::ViewAnalytics:
                case Permission::ExportData:
                case Permission::ChangeOwnPassword:
                    return true;
                default:
                    return false;
            }
        case Role::Student:
            return p == Permission::ViewOwnRecord || p == Permission::ExportOwnTranscript ||
                   p == Permission::ChangeOwnPassword;
    }
    return false;
}

std::string_view AccessPolicy::describe(Permission p) {
    switch (p) {
        case Permission::ViewAnyStudent: return "view student records";
        case Permission::ViewOwnRecord: return "view own record";
        case Permission::CreateStudent: return "create students";
        case Permission::UpdateStudent: return "edit students";
        case Permission::SoftDeleteStudent: return "deactivate students";
        case Permission::RestoreStudent: return "restore students";
        case Permission::HardDeleteStudent: return "permanently delete students";
        case Permission::ManageCourses: return "manage courses";
        case Permission::HardDeleteCourse: return "permanently delete courses";
        case Permission::ManageEnrollments: return "manage enrollments";
        case Permission::RecordGrades: return "record grades";
        case Permission::ViewAnalytics: return "view analytics";
        case Permission::ExportData: return "export data";
        case Permission::ExportOwnTranscript: return "export own transcript";
        case Permission::ManageBackups: return "manage backups";
        case Permission::ManageUsers: return "manage user accounts";
        case Permission::ViewAuditLog: return "view the audit log";
        case Permission::ImportLegacy: return "import legacy data";
        case Permission::ChangeOwnPassword: return "change own password";
    }
    return "perform this action";
}

void AccessPolicy::require(const User& actor, Permission p, storage::AuditLogger& audit) {
    if (!actor.active) {
        audit.warn("ACCESS_DENIED", actor.username, "disabled account attempted to " + std::string(describe(p)));
        throw AuthException("This account is disabled.");
    }
    if (!allows(actor.role, p)) {
        audit.warn("ACCESS_DENIED", actor.username,
                   "role " + std::string(toString(actor.role)) + " attempted to " + std::string(describe(p)));
        throw AuthException("Access denied: your role (" + std::string(toString(actor.role)) +
                            ") may not " + std::string(describe(p)) + ".");
    }
}

void AccessPolicy::requireStudentAccess(const User& actor, std::string_view studentId, storage::AuditLogger& audit) {
    if (actor.role == Role::Student) {
        require(actor, Permission::ViewOwnRecord, audit);
        if (actor.linkedStudentId != studentId) {
            audit.warn("ACCESS_DENIED", actor.username,
                       "student account attempted to read record " + std::string(studentId));
            throw AuthException("Access denied: you may only view your own record.");
        }
        return;
    }
    require(actor, Permission::ViewAnyStudent, audit);
}

}  // namespace sms
