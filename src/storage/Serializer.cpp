#include "sms/storage/Serializer.hpp"

#include <sstream>

#include "sms/core/Crypto.hpp"
#include "sms/core/Exceptions.hpp"
#include "sms/core/Text.hpp"

namespace sms::storage {

namespace {

void appendRecord(std::string& out, std::size_t& count, std::initializer_list<std::string> fields) {
    bool first = true;
    for (const auto& f : fields) {
        if (!first) out.push_back('\t');
        out += text::escapeField(f);
        first = false;
    }
    out.push_back('\n');
    ++count;
}

[[noreturn]] void fail(std::size_t line, const std::string& what) {
    throw DatabaseException("Database file corrupt at line " + std::to_string(line) + ": " + what);
}

bool parseBool(const std::string& s, std::size_t line) {
    if (s == "1") return true;
    if (s == "0") return false;
    fail(line, "expected 0 or 1 but found '" + text::sanitizeForLog(s, 20) + "'");
}

double parseNumber(const std::string& s, std::size_t line, const char* what) {
    const auto v = text::parseDouble(s);
    if (!v) fail(line, std::string("invalid number for ") + what);
    return *v;
}

}  // namespace

std::string serialize(const Database& db) {
    std::string out;
    std::size_t count = 0;
    out += std::string(kMagic) + "\t" + std::to_string(kSchemaVersion) + "\n";
    for (const auto& u : db.users) {
        appendRecord(out, count, {"U", u.username, std::string(toString(u.role)), u.passwordHash,
                                  u.linkedStudentId, u.active ? "1" : "0", u.mustChangePassword ? "1" : "0",
                                  u.createdAt});
    }
    for (const auto& s : db.students) {
        appendRecord(out, count, {"S", s.id, s.name, s.email, s.phone, s.department,
                                  s.dateOfBirth.value_or(""), s.enrolledOn, s.active ? "1" : "0", s.deletedAt});
    }
    for (const auto& c : db.courses) {
        appendRecord(out, count, {"C", c.code, c.title, text::formatFixed(c.credits, 2),
                                  text::formatFixed(c.ects, 2), c.department, c.active ? "1" : "0"});
    }
    for (const auto& e : db.enrollments) {
        appendRecord(out, count, {"E", e.studentId, e.courseCode, e.term,
                                  e.mark ? text::formatFixed(*e.mark, 2) : std::string(), e.recordedAt});
    }
    out += "END\t" + std::to_string(count) + "\t" + crypto::sha256Hex(out) + "\n";
    return out;
}

Database deserialize(std::string_view content) {
    if (content.empty()) throw DatabaseException("Database file is empty");
    if (content.back() != '\n') throw DatabaseException("Database file is truncated (no trailing newline)");

    // Locate the trailer line.
    const std::size_t lastNl = content.rfind('\n', content.size() - 2);
    const std::size_t trailerStart = (lastNl == std::string_view::npos) ? 0 : lastNl + 1;
    const std::string_view trailer = content.substr(trailerStart, content.size() - trailerStart - 1);
    const auto tparts = text::split(trailer, '\t');
    if (tparts.size() != 3 || tparts[0] != "END") {
        throw DatabaseException("Database file is truncated or has no END trailer");
    }
    const std::string_view body = content.substr(0, trailerStart);
    if (crypto::sha256Hex(body) != tparts[2]) {
        throw DatabaseException("Database checksum mismatch - the file was modified or damaged");
    }

    Database db;
    std::size_t lineNo = 0;
    std::size_t records = 0;
    std::size_t pos = 0;
    while (pos < body.size()) {
        const std::size_t nl = body.find('\n', pos);
        const std::string_view line = body.substr(pos, nl - pos);
        pos = nl + 1;
        ++lineNo;

        std::vector<std::string> f = text::split(line, '\t');
        for (auto& field : f) {
            auto un = text::unescapeField(field);
            if (!un) fail(lineNo, "bad escape sequence");
            field = std::move(*un);
        }

        if (lineNo == 1) {
            if (f.size() != 2 || f[0] != kMagic) fail(lineNo, "missing SMSDB magic header");
            if (f[1] != std::to_string(kSchemaVersion)) {
                fail(lineNo, "unsupported schema version '" + text::sanitizeForLog(f[1], 10) + "'");
            }
            continue;
        }
        ++records;
        const std::string& kind = f[0];
        if (kind == "U") {
            if (f.size() != 8) fail(lineNo, "user record needs 8 fields");
            User u;
            u.username = f[1];
            const auto role = parseRole(f[2]);
            if (!role) fail(lineNo, "unknown role");
            u.role = *role;
            u.passwordHash = f[3];
            u.linkedStudentId = f[4];
            u.active = parseBool(f[5], lineNo);
            u.mustChangePassword = parseBool(f[6], lineNo);
            u.createdAt = f[7];
            db.users.push_back(std::move(u));
        } else if (kind == "S") {
            if (f.size() != 10) fail(lineNo, "student record needs 10 fields");
            Student s;
            s.id = f[1]; s.name = f[2]; s.email = f[3]; s.phone = f[4]; s.department = f[5];
            if (!f[6].empty()) s.dateOfBirth = f[6];
            s.enrolledOn = f[7];
            s.active = parseBool(f[8], lineNo);
            s.deletedAt = f[9];
            db.students.push_back(std::move(s));
        } else if (kind == "C") {
            if (f.size() != 7) fail(lineNo, "course record needs 7 fields");
            Course c;
            c.code = f[1]; c.title = f[2];
            c.credits = parseNumber(f[3], lineNo, "credits");
            c.ects = parseNumber(f[4], lineNo, "ECTS");
            c.department = f[5];
            c.active = parseBool(f[6], lineNo);
            db.courses.push_back(std::move(c));
        } else if (kind == "E") {
            if (f.size() != 6) fail(lineNo, "enrollment record needs 6 fields");
            Enrollment e;
            e.studentId = f[1]; e.courseCode = f[2]; e.term = f[3];
            if (!f[4].empty()) e.mark = parseNumber(f[4], lineNo, "mark");
            e.recordedAt = f[5];
            db.enrollments.push_back(std::move(e));
        } else {
            fail(lineNo, "unknown record type '" + text::sanitizeForLog(kind, 10) + "'");
        }
    }
    if (lineNo == 0) throw DatabaseException("Database file has no header");
    if (std::to_string(records) != tparts[1]) {
        throw DatabaseException("Record count mismatch: trailer says " + tparts[1] + ", found " +
                                std::to_string(records));
    }
    db.validateIntegrity();
    return db;
}

}  // namespace sms::storage
