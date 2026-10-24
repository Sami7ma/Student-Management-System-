#pragma once
#include <string>

namespace sms {

struct Course {
    std::string code;        // e.g. "COSC1011" (unique, upper-case)
    std::string title;
    double credits = 3.0;    // credit hours, used as GPA weight
    double ects = 5.0;
    std::string department;
    bool active = true;      // false == retired from the catalogue (soft-deleted)
};

}  // namespace sms
