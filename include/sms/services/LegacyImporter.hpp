#pragma once
// One-way importer for the original program's "students.dat" text format:
//   STUDENT / id / name / major / phone / { COURSE / title / code / credit / ects / grade }* / ENDSTUDENT
// Legacy records store letter grades only, so each is converted to the middle of its percentage
// band (GPA is preserved). Imported enrollments use the term "LEGACY".
#include <filesystem>
#include <string>
#include <vector>

#include "sms/services/BackupService.hpp"

namespace sms {

struct ImportReport {
    std::size_t studentsImported = 0;
    std::size_t coursesCreated = 0;
    std::size_t enrollmentsImported = 0;
    std::size_t studentsSkipped = 0;
    std::vector<std::string> warnings;
};

class LegacyImporter {
public:
    LegacyImporter(storage::DataStore& store, storage::AuditLogger& audit, BackupService& backups)
        : store_(store), audit_(audit), backups_(backups) {}

    ImportReport import(const User& actor, const std::filesystem::path& legacyFile);

private:
    storage::DataStore& store_;
    storage::AuditLogger& audit_;
    BackupService& backups_;
};

}  // namespace sms
