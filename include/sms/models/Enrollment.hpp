#pragma once
#include <optional>
#include <string>

namespace sms {

/// One student taking one course in one term. Identity is (studentId, courseCode, term).
struct Enrollment {
    std::string studentId;
    std::string courseCode;
    std::string term;                 // e.g. "2025-S1"
    std::optional<double> mark;       // percentage 0..100; empty until graded
    std::string recordedAt;           // timestamp of last change
};

}  // namespace sms
