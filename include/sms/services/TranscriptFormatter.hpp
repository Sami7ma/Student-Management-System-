#pragma once
#include <string>

#include "sms/services/AnalyticsEngine.hpp"

namespace sms {

/// Renders a transcript as printable fixed-width plain text (78 columns, ASCII borders).
std::string formatTranscriptText(const Transcript& transcript);

}  // namespace sms
