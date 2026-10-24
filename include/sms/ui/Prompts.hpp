#pragma once
// Prompt helpers and renderers shared by several screens.
#include <string>

#include "sms/ui/AppContext.hpp"

namespace sms::ui {

std::string askStudentId(AppContext& ctx, const std::string& label = "Student ID");
std::string askCourseCode(AppContext& ctx, const std::string& label = "Course code");
std::string askTerm(AppContext& ctx);
std::string defaultTerm();

/// Profile panel + enrollment table for one student (access-checked via StudentManager).
void renderProfile(AppContext& ctx, const std::string& studentId);
/// Full on-screen transcript with per-term tables and cumulative summary.
void renderTranscript(AppContext& ctx, const Transcript& transcript);

}  // namespace sms::ui
