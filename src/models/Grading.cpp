#include "sms/models/Grading.hpp"

namespace sms::grading {

std::string letterFromMark(double mark) {
    if (mark >= 85) return "A";
    if (mark >= 80) return "A-";
    if (mark >= 75) return "B+";
    if (mark >= 70) return "B";
    if (mark >= 65) return "B-";
    if (mark >= 60) return "C+";
    if (mark >= 50) return "C";
    if (mark >= 45) return "C-";
    if (mark >= 40) return "D";
    return "F";
}

double pointsFromLetter(std::string_view g) {
    if (g == "A+" || g == "A") return 4.0;
    if (g == "A-") return 3.75;
    if (g == "B+") return 3.5;
    if (g == "B") return 3.0;
    if (g == "B-") return 2.75;
    if (g == "C+") return 2.5;
    if (g == "C") return 2.0;
    if (g == "C-") return 1.75;
    if (g == "D") return 1.0;
    return 0.0;
}

double pointsFromMark(double mark) { return pointsFromLetter(letterFromMark(mark)); }

bool isPassing(double mark) { return mark >= kPassMark; }

double representativeMark(std::string_view g, bool& known) {
    known = true;
    if (g == "A+") return 95;
    if (g == "A") return 90;
    if (g == "A-") return 82;
    if (g == "B+") return 77;
    if (g == "B") return 72;
    if (g == "B-") return 67;
    if (g == "C+") return 62;
    if (g == "C") return 55;
    if (g == "C-") return 47;
    if (g == "D") return 42;
    if (g == "F") return 20;
    known = false;
    return 0;
}

}  // namespace sms::grading
