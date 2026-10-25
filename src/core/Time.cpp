#include "sms/core/Time.hpp"

#include <array>
#include <chrono>
#include <ctime>

namespace sms::timeutil {

namespace {

std::tm localTm(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

std::string formatNow(const char* fmt) {
    const std::tm tmv = localTm(std::time(nullptr));
    std::array<char, 64> buf{};
    const std::size_t n = std::strftime(buf.data(), buf.size(), fmt, &tmv);
    return std::string(buf.data(), n);
}

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m) {
    static constexpr std::array<int, 12> kDays = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeap(y)) ? 29 : kDays[static_cast<std::size_t>(m - 1)];
}

bool parseParts(std::string_view d, int& y, int& m, int& day) {
    if (d.size() != 10 || d[4] != '-' || d[7] != '-') return false;
    auto num = [&](std::size_t pos, std::size_t len, int& out) {
        out = 0;
        for (std::size_t i = pos; i < pos + len; ++i) {
            if (d[i] < '0' || d[i] > '9') return false;
            out = out * 10 + (d[i] - '0');
        }
        return true;
    };
    return num(0, 4, y) && num(5, 2, m) && num(8, 2, day);
}

}  // namespace

std::string nowTimestamp() { return formatNow("%Y-%m-%d %H:%M:%S"); }
std::string today() { return formatNow("%Y-%m-%d"); }
std::string fileStamp() { return formatNow("%Y%m%d_%H%M%S"); }

std::int64_t epochSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool isValidDate(std::string_view date) {
    int y = 0, m = 0, d = 0;
    if (!parseParts(date, y, m, d)) return false;
    if (y < 1900 || y > 2200 || m < 1 || m > 12 || d < 1) return false;
    return d <= daysInMonth(y, m);
}

int ageOn(std::string_view dob, std::string_view onDate) {
    int by = 0, bm = 0, bd = 0, oy = 0, om = 0, od = 0;
    if (!parseParts(dob, by, bm, bd) || !parseParts(onDate, oy, om, od)) return 0;
    int age = oy - by;
    if (om < bm || (om == bm && od < bd)) --age;
    return age;
}

}  // namespace sms::timeutil
