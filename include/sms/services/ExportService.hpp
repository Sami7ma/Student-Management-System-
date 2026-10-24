#pragma once
// Report export. CSV is RFC-4180 with a UTF-8 BOM (so Excel shows non-Latin names correctly)
// and a formula-injection guard; Markdown output escapes table syntax.
#include <filesystem>
#include <string>
#include <vector>

#include "sms/services/AnalyticsEngine.hpp"

namespace sms {

class ExportService {
public:
    ExportService(storage::DataStore& store, storage::AuditLogger& audit, std::filesystem::path directory)
        : store_(store), audit_(audit), directory_(std::move(directory)) {}

    std::filesystem::path exportStudentsCsv(const User& actor, const std::vector<StudentSummary>& rows);
    std::filesystem::path exportStudentsMarkdown(const User& actor, const std::vector<StudentSummary>& rows);
    std::filesystem::path exportCourseStatsCsv(const User& actor);
    std::filesystem::path exportCourseStatsMarkdown(const User& actor);
    /// Staff may export any transcript; a student only their own.
    std::filesystem::path exportTranscript(const User& actor, std::string_view studentId);

    const std::filesystem::path& directory() const { return directory_; }

private:
    std::filesystem::path write(const std::string& baseName, const std::string& extension, const std::string& content);

    storage::DataStore& store_;
    storage::AuditLogger& audit_;
    std::filesystem::path directory_;
};

}  // namespace sms
