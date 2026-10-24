#pragma once
// Grading scale carried over from the original program (percentage -> letter -> 4.0 points).
#include <string>
#include <string_view>

namespace sms::grading {

constexpr double kPassMark = 40.0;  // letter grade D or better

std::string letterFromMark(double mark);
double pointsFromLetter(std::string_view letter);
double pointsFromMark(double mark);
bool isPassing(double mark);
/// Mid-band percentage for a letter grade; used only when importing legacy data.
double representativeMark(std::string_view letter, bool& known);

}  // namespace sms::grading
