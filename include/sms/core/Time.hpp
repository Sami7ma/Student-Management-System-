#pragma once
// Local-time helpers and strict calendar-date validation (YYYY-MM-DD).
#include <cstdint>
#include <string>
#include <string_view>

namespace sms::timeutil {

std::string nowTimestamp();  // "YYYY-MM-DD HH:MM:SS" (local time)
std::string today();         // "YYYY-MM-DD"
std::string fileStamp();     // "YYYYMMDD_HHMMSS" for file names
std::int64_t epochSeconds();

bool isValidDate(std::string_view date);  // format AND real calendar date
/// Whole years between `dob` and `onDate` (both valid YYYY-MM-DD). May be negative.
int ageOn(std::string_view dob, std::string_view onDate);

}  // namespace sms::timeutil
