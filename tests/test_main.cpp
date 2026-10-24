// Self-contained unit tests (no external framework). Build & run:  make test
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "sms/core/Crypto.hpp"
#include "sms/core/Exceptions.hpp"
#include "sms/core/FileSystem.hpp"
#include "sms/core/Text.hpp"
#include "sms/core/Time.hpp"
#include "sms/models/Grading.hpp"
#include "sms/models/Rules.hpp"
#include "sms/services/AccessPolicy.hpp"
#include "sms/services/AnalyticsEngine.hpp"
#include "sms/services/AuthService.hpp"
#include "sms/services/BackupService.hpp"
#include "sms/services/CourseManager.hpp"
#include "sms/services/EnrollmentManager.hpp"
#include "sms/services/ExportService.hpp"
#include "sms/services/LegacyImporter.hpp"
#include "sms/services/StudentManager.hpp"
#include "sms/services/TranscriptFormatter.hpp"
#include "sms/storage/DataStore.hpp"
#include "sms/storage/Logger.hpp"
#include "sms/storage/Serializer.hpp"
#include "sms/ui/InputValidator.hpp"
#include "sms/ui/TableBuilder.hpp"
#include "sms/ui/TerminalRenderer.hpp"

namespace fs = std::filesystem;
using namespace sms;

// ------------------------------------------------------------------ mini framework
namespace {
struct TestCase {
    const char* name;
    std::function<void()> fn;
};
std::vector<TestCase>& registry() {
    static std::vector<TestCase> r;
    return r;
}
struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};
int g_failures = 0;
int g_checks = 0;
}  // namespace

#define TEST(name)                         \
    static void name();                    \
    static Registrar registrar_##name(#name, name); \
    static void name()

#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) {                                                               \
            ++g_failures;                                                            \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  " #cond "\n"; \
        }                                                                            \
    } while (0)

#define CHECK_THROWS(expr, ExType)                                                   \
    do {                                                                             \
        ++g_checks;                                                                  \
        bool caught_ = false;                                                        \
        try {                                                                        \
            (void)(expr);                                                            \
        } catch (const ExType&) {                                                    \
            caught_ = true;                                                          \
        } catch (...) {                                                              \
        }                                                                            \
        if (!caught_) {                                                              \
            ++g_failures;                                                            \
            std::cerr << "  FAIL " << __FILE__ << ":" << __LINE__ << "  expected " #ExType " from " #expr "\n"; \
        }                                                                            \
    } while (0)

// ------------------------------------------------------------------ fixtures
namespace {

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("sms_test_" + crypto::toHex(crypto::randomBytes(6)));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

struct Fixture {
    TempDir dir;
    storage::AuditLogger audit;
    storage::DataStore store;
    BackupService backups;
    AuthService auth;
    StudentManager students;
    CourseManager courses;
    EnrollmentManager enrollments;
    AnalyticsEngine analytics;
    ExportService exports;
    LegacyImporter legacy;
    User admin, faculty, studentUser;

    Fixture()
        : dir(),
          audit(dir.path / "audit.log"),
          store(storage::FileRepository(dir.path / "sms.db")),
          backups(store, audit, dir.path / "backups"),
          auth(store, audit),
          students(store, audit),
          courses(store, audit),
          enrollments(store, audit),
          analytics(store, audit),
          exports(store, audit, dir.path / "exports"),
          legacy(store, audit, backups) {
        store.load();
    }

    User addUser(const std::string& name, Role role, const std::string& linked = "") {
        User u;
        u.username = name;
        u.role = role;
        u.passwordHash = AuthService::hashPassword("Passw0rdX", 1000);  // fast hash for tests
        u.linkedStudentId = linked;
        u.createdAt = timeutil::nowTimestamp();
        store.transact([&](Database& db) { db.users.push_back(u); });
        return u;
    }

    void seed() {
        admin = addUser("admin", Role::Admin);
        faculty = addUser("teacher", Role::Faculty);
        auto mk = [&](const char* id, const char* name, const char* dept) {
            Student s;
            s.id = id;
            s.name = name;
            s.department = dept;
            students.create(admin, s);
        };
        mk("NSE/1001/16", "Abel Tesfaye", "Computer Science");
        mk("NSE/1002/16", "Beza Zenebe", "Computer Science");
        mk("EEE/2001/16", "Chaltu Bekele", "Electrical Engineering");
        auto mc = [&](const char* code, const char* title, double cr, const char* dept) {
            Course c;
            c.code = code;
            c.title = title;
            c.credits = cr;
            c.ects = 5;
            c.department = dept;
            courses.create(admin, c);
        };
        mc("COSC1011", "Programming I", 3, "Computer Science");
        mc("COSC1012", "Data Structures", 3, "Computer Science");
        mc("EENG2001", "Circuits", 4, "Electrical Engineering");
        enrollments.enroll(admin, "NSE/1001/16", "COSC1011", "2025-S1", 90.0);
        enrollments.enroll(admin, "NSE/1001/16", "COSC1012", "2025-S1", 72.0);
        enrollments.enroll(admin, "NSE/1002/16", "COSC1011", "2025-S1", 55.0);
        enrollments.enroll(admin, "NSE/1002/16", "COSC1012", "2025-S1", 38.0);
        enrollments.enroll(admin, "EEE/2001/16", "EENG2001", "2025-S1", 82.0);
        studentUser = addUser("abel", Role::Student, "NSE/1001/16");
    }
};

struct UiRig {
    std::istringstream in;
    std::ostringstream out;
    ui::Terminal term;
    ui::TerminalRenderer renderer;
    ui::InputValidator v;
    explicit UiRig(const std::string& input)
        : in(input), out(), term(ui::TerminalOptions{}, in, out), renderer(term), v(term, renderer) {}
};

}  // namespace

// ================================================================== crypto
TEST(sha256_known_vectors) {
    CHECK(crypto::sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(crypto::sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(crypto::sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    crypto::Sha256 incremental;  // chunked input must equal one-shot
    incremental.update("ab");
    incremental.update("c");
    const auto d = incremental.finish();
    CHECK(crypto::toHex(d.data(), d.size()) == crypto::sha256Hex("abc"));
}

TEST(hmac_and_pbkdf2_vectors) {
    const std::vector<std::uint8_t> key(20, 0x0b);
    const std::string msg = "Hi There";
    crypto::HmacSha256 h(key.data(), key.size());
    const auto mac = h.compute(reinterpret_cast<const std::uint8_t*>(msg.data()), msg.size());
    CHECK(crypto::toHex(mac.data(), mac.size()) == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    const crypto::Bytes salt = {'s', 'a', 'l', 't'};
    CHECK(crypto::toHex(crypto::pbkdf2HmacSha256("password", salt, 1, 32)) ==
          "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    CHECK(crypto::toHex(crypto::pbkdf2HmacSha256("password", salt, 4096, 32)) ==
          "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a");
    const std::string longSalt = "saltSALTsaltSALTsaltSALTsaltSALTsalt";
    const crypto::Bytes ls(longSalt.begin(), longSalt.end());
    CHECK(crypto::toHex(crypto::pbkdf2HmacSha256("passwordPASSWORDpassword", ls, 4096, 40)) ==
          "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9");
    CHECK_THROWS(crypto::pbkdf2HmacSha256("x", salt, 0, 32), ValidationException);
}

TEST(hex_and_constant_time) {
    CHECK(crypto::fromHex("00ff10").has_value());
    CHECK(!crypto::fromHex("0g").has_value());
    CHECK(!crypto::fromHex("abc").has_value());
    CHECK(crypto::constantTimeEquals({1, 2, 3}, {1, 2, 3}));
    CHECK(!crypto::constantTimeEquals({1, 2, 3}, {1, 2, 4}));
    CHECK(!crypto::constantTimeEquals({1, 2}, {1, 2, 3}));
    CHECK(crypto::randomBytes(16) != crypto::randomBytes(16));
}

// ================================================================== text
TEST(text_width_truncate_escape) {
    CHECK(text::displayWidth("abc") == 3);
    CHECK(text::displayWidth("\xE6\xBC\xA2\xE5\xAD\x97") == 4);                // two CJK glyphs
    CHECK(text::displayWidth("\x1b[31mred\x1b[0m") == 3);                      // ANSI ignored
    CHECK(text::displayWidth("e\xCC\x81") == 1);                               // combining accent
    CHECK(text::truncate("abcdefghij", 5) == "abcd\xE2\x80\xA6");
    CHECK(text::displayWidth(text::truncate("\xE6\xBC\xA2\xE5\xAD\x97\xE6\xBC\xA2", 4)) <= 4);
    CHECK(text::isValidUtf8("\xE1\x8A\xA0\xE1\x89\xA0\xE1\x88\x8D"));        // Ge'ez text
    CHECK(!text::isValidUtf8("\xC0\xAF"));                                     // overlong
    CHECK(!text::isValidUtf8("\xED\xA0\x80"));                                 // surrogate
    CHECK(!text::isValidUtf8("abc\xE2\x82"));                                  // truncated
    const std::string nasty = "a\\b\tc\nd\re";
    CHECK(text::unescapeField(text::escapeField(nasty)).value() == nasty);
    CHECK(!text::unescapeField("bad\\x").has_value());
    CHECK(!text::unescapeField("trailing\\").has_value());
}

TEST(text_numbers_strict) {
    CHECK(text::parseDouble("72.5").value() == 72.5);
    CHECK(!text::parseDouble("nan").has_value());
    CHECK(!text::parseDouble("inf").has_value());
    CHECK(!text::parseDouble("12abc").has_value());
    CHECK(!text::parseDouble("1e999").has_value());
    CHECK(!text::parseDouble("").has_value());
    CHECK(!text::parseDouble("0x10").has_value());
    CHECK(text::parseInteger("-42").value() == -42);
    CHECK(!text::parseInteger("4 2").has_value());
    CHECK(!text::parseInteger("99999999999999999999").has_value());
    CHECK(!text::parseInteger("+").has_value());
}

TEST(csv_and_markdown_escaping) {
    CHECK(text::csvEscape("plain") == "plain");
    CHECK(text::csvEscape("a,b") == "\"a,b\"");
    CHECK(text::csvEscape("say \"hi\"") == "\"say \"\"hi\"\"\"");
    CHECK(text::csvEscape("=SUM(A1)") == "'=SUM(A1)");           // formula injection neutralised
    CHECK(text::csvEscape("@cmd") == "'@cmd");
    CHECK(text::csvEscape("+251911000000") == "'+251911000000");
    CHECK(text::markdownEscape("a|b") == "a\\|b");
    CHECK(text::sanitizeForLog("evil\nINFO | fake", 100).find('\n') == std::string::npos);
    CHECK(text::sanitizeForLog("a|b", 100) == "a/b");
}

// ================================================================== rules
TEST(rules_student_id_email_phone) {
    CHECK(!rules::checkStudentId("NSE/1001/16"));
    CHECK(rules::checkStudentId("nse/1001/16").has_value());   // must be normalised first
    CHECK(!rules::checkStudentId(rules::normalizeStudentId(" nse/1001/16 ")));
    CHECK(rules::checkStudentId("NSE/10/16").has_value());
    CHECK(rules::checkStudentId("NSE/1001/2016").has_value());
    CHECK(rules::checkStudentId("N/1001/16").has_value());
    CHECK(rules::checkStudentId("").has_value());
    CHECK(rules::checkStudentId("NSE/1001/16/../../etc").has_value());
    CHECK(!rules::checkEmail("abel.t@example.com"));
    CHECK(rules::checkEmail("abel@@example.com").has_value());
    CHECK(rules::checkEmail("abel@example").has_value());
    CHECK(rules::checkEmail(".abel@example.com").has_value());
    CHECK(rules::checkEmail("a..b@example.com").has_value());
    CHECK(rules::checkEmail(std::string(300, 'a') + "@example.com").has_value());
    CHECK(!rules::checkPhone(rules::normalizePhone("+251 91-000 0001")));
    CHECK(!rules::checkPhone("0910000001"));
    CHECK(rules::checkPhone("12345").has_value());
    CHECK(rules::checkPhone("09100abc01").has_value());
    CHECK(rules::checkPhone("").has_value());
}

TEST(rules_names_courses_numbers) {
    CHECK(!rules::checkPersonName("Abel Tesfaye"));
    CHECK(!rules::checkPersonName("O'Brien-Smith Jr."));
    CHECK(!rules::checkPersonName("\xE1\x8A\xA0\xE1\x89\xA0\xE1\x88\x8D"));  // non-Latin letters allowed
    CHECK(rules::checkPersonName("R2D2").has_value());
    CHECK(rules::checkPersonName("A").has_value());
    CHECK(rules::checkPersonName(" leading").has_value());
    CHECK(rules::checkPersonName(std::string("tab\tname")).has_value());
    CHECK(rules::checkPersonName(std::string(101, 'x')).has_value());
    CHECK(!rules::checkCourseCode("COSC1011"));
    CHECK(rules::checkCourseCode("C1").has_value());
    CHECK(rules::checkCourseCode("COSC10").has_value());
    CHECK(rules::checkCourseCode("1011COSC").has_value());
    CHECK(!rules::checkCredits(3.0));
    CHECK(rules::checkCredits(0.0).has_value());
    CHECK(rules::checkCredits(13).has_value());
    CHECK(rules::checkMark(100).has_value() == false);
    CHECK(rules::checkMark(100.01).has_value());
    CHECK(rules::checkMark(-0.1).has_value());
    CHECK(!rules::checkGpaRange(0.0, 4.0));
    CHECK(rules::checkGpaRange(-1, 4).has_value());
    CHECK(rules::checkGpaRange(1, 4.5).has_value());
    CHECK(rules::checkGpaRange(3.5, 2.0).has_value());
    CHECK(!rules::checkTerm("2025-S1"));
    CHECK(rules::checkTerm("20 25").has_value());
}

TEST(rules_dates_age_password) {
    CHECK(timeutil::isValidDate("2024-02-29"));
    CHECK(!timeutil::isValidDate("2023-02-29"));
    CHECK(!timeutil::isValidDate("2023-13-01"));
    CHECK(!timeutil::isValidDate("2023-1-01"));
    CHECK(!timeutil::isValidDate("abcd-ef-gh"));
    CHECK(timeutil::ageOn("2000-06-15", "2025-06-14") == 24);
    CHECK(timeutil::ageOn("2000-06-15", "2025-06-15") == 25);
    CHECK(!rules::checkDateOfBirth("2000-01-01", "2026-10-24"));
    CHECK(rules::checkDateOfBirth("2020-01-01", "2026-10-24").has_value());   // too young
    CHECK(rules::checkDateOfBirth("1900-01-01", "2026-10-24").has_value());   // too old
    CHECK(rules::checkDateOfBirth("2030-01-01", "2026-10-24").has_value());   // future
    CHECK(!rules::checkPassword("Passw0rdX"));
    CHECK(rules::checkPassword("short1A").has_value());
    CHECK(rules::checkPassword("alllowercase1").has_value());
    CHECK(rules::checkPassword("NoDigitsHere").has_value());
    CHECK(rules::checkPassword(std::string(65, 'A') + "a1").has_value());
    CHECK(!rules::checkUsername("admin"));
    CHECK(rules::checkUsername("Admin").has_value());
    CHECK(rules::checkUsername("1abc").has_value());
    CHECK(rules::checkUsername("a b").has_value());
}

TEST(grading_scale_boundaries) {
    CHECK(grading::letterFromMark(85) == "A");
    CHECK(grading::letterFromMark(84.99) == "A-");
    CHECK(grading::letterFromMark(50) == "C");
    CHECK(grading::letterFromMark(49.99) == "C-");
    CHECK(grading::letterFromMark(40) == "D");
    CHECK(grading::letterFromMark(39.99) == "F");
    CHECK(grading::pointsFromMark(90) == 4.0);
    CHECK(grading::pointsFromMark(10) == 0.0);
    CHECK(grading::isPassing(40) && !grading::isPassing(39.9));
}

// ================================================================== storage
TEST(serializer_roundtrip_and_unicode) {
    Database db;
    Student s;
    s.id = "NSE/1001/16";
    s.name = "\xE1\x8A\xA0\xE1\x89\xA0\xE1\x88\x8D Tesfaye";
    s.department = "Computer Science";
    s.email = "a@b.co";
    s.phone = "+251910000001";
    s.dateOfBirth = "2001-05-05";
    s.enrolledOn = "2024-09-01";
    db.students.push_back(s);
    Course c;
    c.code = "COSC1011";
    c.title = "Programming I";
    c.credits = 3;
    c.ects = 5;
    c.department = "Computer Science";
    db.courses.push_back(c);
    db.enrollments.push_back({"NSE/1001/16", "COSC1011", "2025-S1", 77.5, "2025-01-01 10:00:00"});
    db.enrollments.push_back({"NSE/1001/16", "COSC1011", "2025-S2", std::nullopt, "2025-01-01 10:00:00"});
    User u;
    u.username = "admin";
    u.role = Role::Admin;
    u.passwordHash = "pbkdf2-sha256$1$00$00";
    db.users.push_back(u);

    const std::string blob = storage::serialize(db);
    CHECK(blob.rfind("SMSDB\t2\n", 0) == 0);
    const Database back = storage::deserialize(blob);
    CHECK(back.students.size() == 1 && back.students[0].name == s.name);
    CHECK(back.students[0].dateOfBirth.value() == "2001-05-05");
    CHECK(back.enrollments.size() == 2 && back.enrollments[0].mark.value() == 77.5 && !back.enrollments[1].mark);
    CHECK(back.users[0].role == Role::Admin);
}

TEST(serializer_detects_corruption) {
    Fixture f;
    f.seed();
    const std::string good = storage::serialize(f.store.db());
    CHECK(!storage::deserialize(good).students.empty());

    std::string flipped = good;
    flipped[good.find("Abel")] = 'X';                              // single byte changed
    CHECK_THROWS(storage::deserialize(flipped), DatabaseException);
    CHECK_THROWS(storage::deserialize(good.substr(0, good.size() - 12)), DatabaseException);  // truncated
    CHECK_THROWS(storage::deserialize(""), DatabaseException);
    CHECK_THROWS(storage::deserialize("garbage\n"), DatabaseException);
    CHECK_THROWS(storage::deserialize(good + "extra line\n"), DatabaseException);
    std::string noNewline = good;
    noNewline.pop_back();
    CHECK_THROWS(storage::deserialize(noNewline), DatabaseException);
}

TEST(serializer_validates_schema_and_integrity) {
    // Records are well-formed and correctly checksummed, but semantically invalid.
    Database dup;
    Student s{"NSE/1001/16", "Abel Tesfaye", "", "", "CS", std::nullopt, "2024-09-01", true, ""};
    dup.students = {s, s};
    CHECK_THROWS(storage::deserialize(storage::serialize(dup)), DatabaseException);  // duplicate ID

    Database orphan;
    orphan.enrollments.push_back({"NSE/1001/16", "COSC1011", "2025-S1", 50.0, ""});
    CHECK_THROWS(storage::deserialize(storage::serialize(orphan)), DatabaseException);  // dangling refs

    Database badGpa = dup;
    badGpa.students.pop_back();
    badGpa.courses.push_back({"COSC1011", "Prog", 3, 5, "CS", true});
    badGpa.enrollments.push_back({"NSE/1001/16", "COSC1011", "2025-S1", 150.0, ""});
    CHECK_THROWS(storage::deserialize(storage::serialize(badGpa)), DatabaseException);  // mark > 100

    Database badEmail;
    Student e = s;
    e.email = "not-an-email";
    badEmail.students.push_back(e);
    CHECK_THROWS(storage::deserialize(storage::serialize(badEmail)), DatabaseException);

    Database orphanUser;
    User u;
    u.username = "abel";
    u.role = Role::Student;
    u.linkedStudentId = "NSE/9999/16";
    u.passwordHash = "x";
    orphanUser.users.push_back(u);
    CHECK_THROWS(storage::deserialize(storage::serialize(orphanUser)), DatabaseException);
}

TEST(repository_atomic_save_and_load) {
    TempDir dir;
    storage::FileRepository repo(dir.path / "sub" / "sms.db");
    CHECK(!repo.exists());
    CHECK(repo.load().students.empty());                  // missing file == empty database
    Database db;
    db.students.push_back({"NSE/1001/16", "Abel Tesfaye", "", "", "CS", std::nullopt, "2024-09-01", true, ""});
    repo.save(db);
    CHECK(repo.exists());
    CHECK(!fs::exists(dir.path / "sub" / "sms.db.tmp"));  // temp file renamed away
    CHECK(repo.load().students.size() == 1);
    // a stale half-written temp file from a crash must not affect loading
    std::ofstream(dir.path / "sub" / "sms.db.tmp") << "partial garbage";
    CHECK(repo.load().students.size() == 1);
    db.students.clear();
    repo.save(db);                                        // overwrites cleanly
    CHECK(repo.load().students.empty());
    std::ofstream(dir.path / "sub" / "sms.db", std::ios::trunc) << "corrupted";
    CHECK_THROWS(repo.load(), DatabaseException);
}

TEST(datastore_transaction_rolls_back) {
    Fixture f;
    f.seed();
    const std::size_t before = f.store.db().students.size();
    CHECK_THROWS(f.store.transact([](Database& db) {
        db.students.push_back({"ZZZ/9999/20", "Temp Person", "", "", "CS", std::nullopt, "2024-09-01", true, ""});
        throw ValidationException("boom");
    }), ValidationException);
    CHECK(f.store.db().students.size() == before);
    CHECK_THROWS(f.store.transact([](Database& db) { db.students.push_back(db.students.front()); }), DatabaseException);
    CHECK(f.store.db().students.size() == before);                   // invariant violation rejected
    CHECK(storage::deserialize(fsutil::readAll(f.dir.path / "sms.db")).students.size() == before);  // disk untouched
}

TEST(audit_log_chain_detects_tampering) {
    TempDir dir;
    storage::AuditLogger log(dir.path / "a.log");
    log.info("LOGIN_OK", "admin", "first");
    log.warn("LOGIN_FAILED", "mallory", "bad\npassword | injected");
    log.critical("STUDENT_HARD_DELETE", "admin", "id=X");
    CHECK(log.verify().ok && log.verify().lines == 3);
    CHECK(log.tail(10).size() == 3);                                  // newline injection did not add lines
    {
        storage::AuditLogger reopened(dir.path / "a.log");            // chain continues across restarts
        reopened.info("APP_START", "system", "");
        CHECK(reopened.verify().ok && reopened.verify().lines == 4);
    }
    std::string content = fsutil::readAll(dir.path / "a.log");
    const auto pos = content.find("mallory");
    content.replace(pos, 7, "someone");                               // rewrite history
    std::ofstream(dir.path / "a.log", std::ios::trunc) << content;
    const auto v = log.verify();
    CHECK(!v.ok && v.firstBadLine == 2);
}

TEST(instance_lock_is_exclusive) {
    TempDir dir;
    fsutil::InstanceLock first(dir.path / ".lock");
    CHECK_THROWS(fsutil::InstanceLock(dir.path / ".lock"), DatabaseException);
}

// ================================================================== security
TEST(password_hashing) {
    const std::string h1 = AuthService::hashPassword("Passw0rdX", 1000);
    const std::string h2 = AuthService::hashPassword("Passw0rdX", 1000);
    CHECK(h1 != h2);                                           // unique salt per hash
    CHECK(h1.find("Passw0rdX") == std::string::npos);          // never plaintext
    CHECK(h1.rfind("pbkdf2-sha256$1000$", 0) == 0);
    CHECK(AuthService::verifyPassword("Passw0rdX", h1));
    CHECK(!AuthService::verifyPassword("passw0rdX", h1));
    CHECK(!AuthService::verifyPassword("Passw0rdX", "plaintext"));
    CHECK(!AuthService::verifyPassword("Passw0rdX", "pbkdf2-sha256$0$00$00"));
    CHECK(!AuthService::verifyPassword("Passw0rdX", "pbkdf2-sha256$99999999$00$00"));
    CHECK(!AuthService::verifyPassword("", h1));
}

TEST(login_lockout_and_generic_errors) {
    Fixture f;
    f.seed();
    CHECK(f.auth.login("TEACHER", "Passw0rdX").username == "teacher");           // case-insensitive username
    std::string unknownMsg, wrongMsg;
    try { f.auth.login("nobody", "Passw0rdX"); } catch (const AuthException& e) { unknownMsg = e.what(); }
    try { f.auth.login("teacher", "WrongPass1"); } catch (const AuthException& e) { wrongMsg = e.what(); }
    CHECK(!unknownMsg.empty() && unknownMsg == wrongMsg);                         // no user enumeration
    for (int i = 0; i < 3; ++i) CHECK_THROWS(f.auth.login("teacher", "WrongPass1"), AuthException);
    CHECK_THROWS(f.auth.login("teacher", "WrongPass1"), AuthException);           // 5th failure locks
    std::string lockMsg;
    try { f.auth.login("teacher", "Passw0rdX"); } catch (const AuthException& e) { lockMsg = e.what(); }
    CHECK(lockMsg.find("Too many failed attempts") != std::string::npos);          // even the right password
    CHECK(f.audit.verify().ok);
    const auto tail = f.audit.tail(100);
    bool sawFailure = false, sawLocked = false;
    for (const auto& l : tail) {
        if (l.find("LOGIN_FAILED") != std::string::npos) sawFailure = true;
        if (l.find("LOGIN_LOCKED") != std::string::npos) sawLocked = true;
        CHECK(l.find("Passw0rdX") == std::string::npos);                          // passwords never logged
    }
    CHECK(sawFailure && sawLocked);
}

TEST(bootstrap_only_once_and_disabled_accounts) {
    Fixture f;
    CHECK(f.auth.needsBootstrap());
    CHECK_THROWS(f.auth.bootstrapAdmin("admin", "weak"), ValidationException);
    CHECK_THROWS(f.auth.bootstrapAdmin("Bad Name", "Passw0rdX"), ValidationException);
    const User admin = f.auth.bootstrapAdmin("admin", "Passw0rdX");
    CHECK(admin.role == Role::Admin && !f.auth.needsBootstrap());
    CHECK_THROWS(f.auth.bootstrapAdmin("admin2", "Passw0rdX"), AuthException);
    CHECK(f.store.db().users[0].passwordHash.find("Passw0rdX") == std::string::npos);
    CHECK_THROWS(f.auth.setActive(admin, "admin", false), ValidationException);   // cannot disable self / last admin
    f.auth.createUser(admin, "clerk", Role::Faculty, "Clerk1234", "");
    CHECK(f.auth.listUsers(admin).size() == 2);
    f.auth.setActive(admin, "clerk", false);
    CHECK_THROWS(f.auth.login("clerk", "Clerk1234"), AuthException);
    f.auth.setActive(admin, "clerk", true);
    CHECK(f.auth.login("clerk", "Clerk1234").mustChangePassword);
    User clerk = f.auth.login("clerk", "Clerk1234");
    CHECK_THROWS(f.auth.changePassword(clerk, "wrong", "NewPassw0rd"), AuthException);
    CHECK_THROWS(f.auth.changePassword(clerk, "Clerk1234", "Clerk1234"), ValidationException);
    f.auth.changePassword(clerk, "Clerk1234", "NewPassw0rd");
    CHECK(!f.auth.login("clerk", "NewPassw0rd").mustChangePassword);
}

TEST(rbac_matrix_enforced_in_services) {
    Fixture f;
    f.seed();
    Student draft{"NSE/3001/16", "New Person", "", "", "CS", std::nullopt, "", true, ""};
    // Student role
    CHECK_THROWS(f.students.create(f.studentUser, draft), AuthException);
    CHECK_THROWS(f.students.search(f.studentUser, {}, {}), AuthException);
    CHECK_THROWS(f.courses.create(f.studentUser, {"TEST1001", "T", 3, 5, "CS", true}), AuthException);
    CHECK_THROWS(f.enrollments.enroll(f.studentUser, "NSE/1001/16", "COSC1011", "2026-S1"), AuthException);
    CHECK_THROWS(f.enrollments.recordMark(f.studentUser, "NSE/1001/16", "COSC1011", "2025-S1", 100), AuthException);
    CHECK_THROWS(f.analytics.overview(f.studentUser), AuthException);
    CHECK_THROWS(f.auth.listUsers(f.studentUser), AuthException);
    CHECK_THROWS(f.backups.createManual(f.studentUser), AuthException);
    CHECK_THROWS(f.exports.exportCourseStatsCsv(f.studentUser), AuthException);
    CHECK(f.students.find(f.studentUser, "NSE/1001/16").has_value());               // own record only
    CHECK_THROWS(f.students.find(f.studentUser, "NSE/1002/16"), AuthException);
    CHECK_THROWS(f.analytics.transcript(f.studentUser, "NSE/1002/16"), AuthException);
    CHECK_THROWS(f.enrollments.forStudent(f.studentUser, "NSE/1002/16"), AuthException);
    CHECK_THROWS(f.exports.exportTranscript(f.studentUser, "NSE/1002/16"), AuthException);
    CHECK(fs::exists(f.exports.exportTranscript(f.studentUser, "NSE/1001/16")));    // own transcript allowed
    // Faculty role
    CHECK(f.students.create(f.faculty, draft).id == "NSE/3001/16");
    CHECK_THROWS(f.students.softDelete(f.faculty, "NSE/3001/16"), AuthException);
    CHECK_THROWS(f.students.hardDelete(f.faculty, "NSE/3001/16"), AuthException);
    CHECK_THROWS(f.students.restore(f.faculty, "NSE/3001/16"), AuthException);
    CHECK_THROWS(f.courses.hardDelete(f.faculty, "COSC1011"), AuthException);
    CHECK_THROWS(f.auth.createUser(f.faculty, "x1y2z3", Role::Admin, "Passw0rdX", ""), AuthException);
    CHECK_THROWS(f.backups.list(f.faculty), AuthException);
    CHECK_THROWS(f.legacy.import(f.faculty, f.dir.path / "none.dat"), AuthException);
    // Disabled actor is refused everywhere
    User disabled = f.admin;
    disabled.active = false;
    CHECK_THROWS(f.students.search(disabled, {}, {}), AuthException);
    // Denials are audited
    bool denied = false;
    for (const auto& l : f.audit.tail(500)) denied = denied || l.find("ACCESS_DENIED") != std::string::npos;
    CHECK(denied);
}

// ================================================================== services
TEST(student_crud_lifecycle) {
    Fixture f;
    f.seed();
    Student d{" nse/4001/16 ", "  Dawit   Haile ", "dawit@example.com", "+251 91 000 0004", "Computer  Science",
              std::string("2002-03-04"), "", true, ""};
    const Student created = f.students.create(f.admin, d);
    CHECK(created.id == "NSE/4001/16" && created.name == "Dawit Haile");          // normalised
    CHECK(created.phone == "+251910000004" && created.department == "Computer Science");
    CHECK(created.enrolledOn == timeutil::today());
    CHECK_THROWS(f.students.create(f.admin, d), ValidationException);              // duplicate ID
    Student bad = d;
    bad.id = "NSE/4002/16";
    bad.email = "bogus";
    CHECK_THROWS(f.students.create(f.admin, bad), ValidationException);
    CHECK(f.store.db().findStudent("NSE/4002/16") == nullptr);                    // nothing half-created

    Student edited = created;
    edited.department = "Information Systems";
    f.students.update(f.admin, edited);
    CHECK(f.store.db().findStudent("NSE/4001/16")->department == "Information Systems");
    Student ghost = created;
    ghost.id = "NSE/9999/16";
    CHECK_THROWS(f.students.update(f.admin, ghost), NotFoundException);

    f.students.softDelete(f.admin, "NSE/4001/16");
    CHECK(!f.store.db().findStudent("NSE/4001/16")->active);
    CHECK_THROWS(f.students.softDelete(f.admin, "NSE/4001/16"), ValidationException);
    CHECK_THROWS(f.students.update(f.admin, edited), ValidationException);         // must restore first
    CHECK_THROWS(f.enrollments.enroll(f.admin, "NSE/4001/16", "COSC1011", "2026-S1"), ValidationException);
    SearchCriteria visible;
    for (const auto& r : f.students.search(f.admin, visible, {})) CHECK(r.student.id != "NSE/4001/16");
    SearchCriteria bin;
    bin.onlyInactive = true;
    CHECK(f.students.search(f.admin, bin, {}).size() == 1);
    f.students.restore(f.admin, "NSE/4001/16");
    CHECK(f.store.db().findStudent("NSE/4001/16")->active);

    f.students.hardDelete(f.admin, "NSE/1001/16");                                  // cascades
    CHECK(f.store.db().findStudent("NSE/1001/16") == nullptr);
    for (const auto& e : f.store.db().enrollments) CHECK(e.studentId != "NSE/1001/16");
    CHECK(f.store.db().findUser("abel") == nullptr);
    CHECK_THROWS(f.students.hardDelete(f.admin, "NSE/1001/16"), NotFoundException);
}

TEST(search_filter_sort) {
    Fixture f;
    f.seed();
    SearchCriteria c;
    c.nameContains = "AB";
    auto rows = f.students.search(f.admin, c, {});
    CHECK(rows.size() == 1 && rows[0].student.id == "NSE/1001/16");                // case-insensitive partial
    c = {};
    c.department = "computer";
    CHECK(f.students.search(f.admin, c, {}).size() == 2);
    c = {};
    c.idContains = "eee/";
    CHECK(f.students.search(f.admin, c, {}).size() == 1);
    c = {};
    c.gpaMin = 3.0;
    c.gpaMax = 4.0;
    CHECK(f.students.search(f.admin, c, {SortKey::Gpa, false}).size() == 2);
    c.gpaMin = 3.8;
    CHECK(f.students.search(f.admin, c, {}).empty());
    c.gpaMin = 3.9;
    c.gpaMax = 1.0;
    CHECK_THROWS(f.students.search(f.admin, c, {}), ValidationException);
    c.gpaMin = -1.0;
    c.gpaMax = 2.0;
    CHECK_THROWS(f.students.search(f.admin, c, {}), ValidationException);
    c = {};
    auto byGpa = f.students.search(f.admin, c, {SortKey::Gpa, false});
    CHECK(byGpa.size() == 3 && byGpa[0].student.id == "EEE/2001/16" && byGpa[2].student.id == "NSE/1002/16");
    auto byName = f.students.search(f.admin, c, {SortKey::Name, true});
    CHECK(byName[0].student.name == "Abel Tesfaye" && byName[2].student.name == "Chaltu Bekele");
    auto byId = f.students.search(f.admin, c, {SortKey::Id, true});
    CHECK(byId[0].student.id == "EEE/2001/16");
    auto byDept = f.students.search(f.admin, c, {SortKey::Department, false});
    CHECK(byDept[0].student.department == "Electrical Engineering");
    CHECK(f.students.departments(f.admin).size() == 2);
}

TEST(enrollment_rules_and_grades) {
    Fixture f;
    f.seed();
    CHECK_THROWS(f.enrollments.enroll(f.admin, "NSE/1001/16", "COSC1011", "2025-S1"), ValidationException);  // duplicate
    CHECK_THROWS(f.enrollments.enroll(f.admin, "NSE/9999/16", "COSC1011", "2026-S1"), NotFoundException);
    CHECK_THROWS(f.enrollments.enroll(f.admin, "NSE/1001/16", "NOPE1234", "2026-S1"), NotFoundException);
    CHECK_THROWS(f.enrollments.enroll(f.admin, "NSE/1001/16", "COSC1011", "bad term!"), ValidationException);
    CHECK_THROWS(f.enrollments.enroll(f.admin, "NSE/1001/16", "COSC1011", "2026-S1", 101.0), ValidationException);
    f.enrollments.enroll(f.admin, "NSE/1001/16", "COSC1011", "2026-S1");                         // retake, new term
    f.enrollments.recordMark(f.admin, "NSE/1001/16", "COSC1011", "2026-S1", 66.666);
    CHECK(f.store.db().findEnrollment("NSE/1001/16", "COSC1011", "2026-S1")->mark.value() == 66.67);
    f.enrollments.clearMark(f.admin, "NSE/1001/16", "COSC1011", "2026-S1");
    CHECK(!f.store.db().findEnrollment("NSE/1001/16", "COSC1011", "2026-S1")->mark);
    CHECK_THROWS(f.enrollments.clearMark(f.admin, "NSE/1001/16", "COSC1011", "2026-S1"), ValidationException);
    f.courses.setActive(f.admin, "COSC1012", false);
    CHECK_THROWS(f.enrollments.enroll(f.admin, "NSE/1002/16", "COSC1012", "2026-S1"), ValidationException);  // retired
    CHECK(AnalyticsEngine::computeGpa(f.store.db(), "NSE/1001/16").gradedCourses == 2);                       // history kept
    f.enrollments.withdraw(f.admin, "NSE/1001/16", "COSC1011", "2026-S1");
    CHECK_THROWS(f.enrollments.withdraw(f.admin, "NSE/1001/16", "COSC1011", "2026-S1"), NotFoundException);
    const std::size_t removed = f.courses.hardDelete(f.admin, "COSC1012");
    CHECK(removed == 2 && f.store.db().findCourse("COSC1012") == nullptr);
}

TEST(analytics_gpa_standing_and_course_stats) {
    Fixture f;
    f.seed();
    const auto abel = f.analytics.summary(f.admin, "NSE/1001/16");
    CHECK(abel.cumulative.gpa == 3.5 && abel.standing == Standing::DeansList);       // (4*3 + 3*3)/6
    const auto beza = f.analytics.summary(f.admin, "NSE/1002/16");
    CHECK(beza.cumulative.gpa == 1.0 && beza.standing == Standing::Probation);       // (2*3 + 0*3)/6
    CHECK(beza.cumulative.earnedCredits == 3.0);
    const auto chaltu = f.analytics.summary(f.admin, "EEE/2001/16");
    CHECK(chaltu.cumulative.gpa == 3.75);
    Student blank{"NSE/5001/16", "No Grades Yet", "", "", "CS", std::nullopt, "", true, ""};
    f.students.create(f.admin, blank);
    CHECK(f.analytics.summary(f.admin, "NSE/5001/16").standing == Standing::NoGrades);
    CHECK(AnalyticsEngine::classify(GpaResult{0, 3, 3, 3, 2.0, 1}) == Standing::GoodStanding);
    CHECK(AnalyticsEngine::classify(GpaResult{0, 3, 3, 3, 1.99, 1}) == Standing::Probation);

    const auto cs = f.analytics.courseStats(f.admin, "COSC1012");                   // marks 72, 38
    CHECK(cs.enrolled == 2 && cs.graded == 2 && cs.passed == 1);
    CHECK(cs.mean == 55.0 && cs.median == 55.0 && cs.minMark == 38.0 && cs.maxMark == 72.0);
    CHECK(cs.passRate == 50.0 && cs.failRate == 50.0 && cs.stddev == 17.0);
    const auto odd = AnalyticsEngine::buildCourseStats(f.store.db(), *f.store.db().findCourse("EENG2001"));
    CHECK(odd.median == 82.0 && odd.passRate == 100.0);
    const auto overview = f.analytics.overview(f.admin);
    CHECK(overview.activeStudents == 4 && overview.deansList == 2 && overview.probation == 1);
    const auto top = f.analytics.topStudents(f.admin, 2);
    CHECK(top.size() == 2 && top[0].student.id == "EEE/2001/16");
    CHECK(f.analytics.departmentStats(f.admin).size() == 3);
    CHECK_THROWS(f.analytics.courseStats(f.admin, "NOPE1234"), NotFoundException);

    const auto tr = f.analytics.transcript(f.admin, "NSE/1001/16");
    CHECK(tr.terms.size() == 1 && tr.terms[0].lines.size() == 2 && tr.cumulative.gpa == 3.5);
    const std::string text = formatTranscriptText(tr);
    CHECK(text.find("OFFICIAL ACADEMIC TRANSCRIPT") != std::string::npos);
    CHECK(text.find("Dean's List") != std::string::npos && text.find("Programming I") != std::string::npos);
}

// ================================================================== backup / export / import
TEST(backup_restore_and_safety) {
    Fixture f;
    f.seed();
    const fs::path snap = f.backups.createManual(f.admin);
    CHECK(fs::exists(snap));
    f.students.hardDelete(f.admin, "EEE/2001/16");
    CHECK(f.store.db().findStudent("EEE/2001/16") == nullptr);
    f.backups.restore(f.admin, snap.filename().string());
    CHECK(f.store.db().findStudent("EEE/2001/16") != nullptr);                       // data came back

    CHECK_THROWS(f.backups.restore(f.admin, "../sms.db"), NotFoundException);        // path traversal
    CHECK_THROWS(f.backups.restore(f.admin, "sms_nonexistent.db"), NotFoundException);

    std::string content = fsutil::readAll(snap);
    content[content.find("Abel")] = 'Z';
    const fs::path bad = f.dir.path / "backups" / "sms_29990101_000000_tampered.db";
    std::ofstream(bad, std::ios::binary) << content;
    const std::string before = storage::serialize(f.store.db());
    CHECK_THROWS(f.backups.restore(f.admin, bad.filename().string()), DatabaseException);
    CHECK(storage::serialize(f.store.db()) == before);                               // live data untouched
    // startup recovery skips the corrupt (newest) snapshot and uses the valid one
    std::ofstream(f.dir.path / "sms.db", std::ios::trunc) << "corrupt";
    CHECK_THROWS(f.store.load(), DatabaseException);
    CHECK(!f.backups.recoverLatestValid("test").empty());
    CHECK(f.store.db().findStudent("NSE/1001/16") != nullptr);
}

TEST(backup_pruning_keeps_newest) {
    Fixture f;
    f.seed();
    BackupService small(f.store, f.audit, f.dir.path / "pruned", 3);
    for (int i = 0; i < 6; ++i) small.snapshot("test", "loop");
    CHECK(small.list(f.admin).size() == 3);
}

TEST(export_csv_and_markdown) {
    Fixture f;
    f.seed();
    auto rows = f.students.search(f.admin, {}, {});
    const fs::path csv = f.exports.exportStudentsCsv(f.admin, rows);
    const std::string c = fsutil::readAll(csv);
    CHECK(c.rfind("\xEF\xBB\xBF", 0) == 0);                                          // BOM for Excel
    CHECK(c.find("Student ID,Name,Email") != std::string::npos);
    CHECK(c.find("NSE/1001/16,Abel Tesfaye") != std::string::npos && c.find("Dean's List") != std::string::npos);
    CHECK(text::split(c, '\n').size() == 5);                                         // header + 3 rows + trailing
    const std::string md = fsutil::readAll(f.exports.exportStudentsMarkdown(f.admin, rows));
    CHECK(md.find("| Student ID |") != std::string::npos && md.find("| --- |") != std::string::npos);
    CHECK(fsutil::readAll(f.exports.exportCourseStatsCsv(f.admin)).find("COSC1011") != std::string::npos);
    CHECK(fsutil::readAll(f.exports.exportCourseStatsMarkdown(f.admin)).find("Course Statistics") != std::string::npos);
    CHECK(fsutil::readAll(f.exports.exportTranscript(f.admin, "NSE/1002/16")).find("Probation") != std::string::npos);
    CHECK(f.exports.exportStudentsCsv(f.admin, rows) != csv);                        // never overwrites
}

TEST(legacy_import_preserves_gpa_and_skips_bad_data) {
    Fixture f;
    f.seed();
    const fs::path legacy = f.dir.path / "students.dat";
    std::ofstream(legacy) <<
        "STUDENT\nNSE/7001/16\nLegacy Abel\nComputer Science\n0910000001\n"
        "COURSE\nProgramming I\nCoSc1011\n3\n3\nA\n"
        "COURSE\nData Structures\nCoSc1012\n3\n3\nB+\n"
        "COURSE\nAlgorithms\nCoSc1013\n3\n3\nA-\n"
        "COURSE\nOperating Systems\nCoSc1014\n3\n3\nB\n"
        "COURSE\nDatabase Systems\nCoSc1015\n3\n3\nA\n"
        "COURSE\nSoftware Eng\nCoSc1016\n3\n3\nB+\n"
        "COURSE\nNetworks\nCoSc1017\n3\n3\nA-\n"
        "COURSE\nAI\nCoSc1018\n3\n3\nB+\n"
        "ENDSTUDENT\n"
        "STUDENT\nBAD-ID\nBroken Person\nCS\n123\nENDSTUDENT\n"
        "STUDENT\nNSE/7002/16\nTruncated Block\nCS\n0910000002\nCOURSE\nOnly Title\n"
        "ENDSTUDENT\n"
        "STUDENT\nNSE/1001/16\nDuplicate Of Existing\nComputer Science\n0910000009\nENDSTUDENT\n";
    const ImportReport r = f.legacy.import(f.admin, legacy);
    CHECK(r.studentsImported == 1 && r.enrollmentsImported == 8);
    CHECK(r.studentsSkipped == 3 && !r.warnings.empty());
    CHECK(f.store.db().findStudent("NSE/7001/16")->name == "Legacy Abel");
    const auto sum = f.analytics.summary(f.admin, "NSE/7001/16");
    CHECK(sum.cumulative.gpa == 3.63);              // 29/8 = 3.625, same as the original program's GPA
    CHECK(f.store.db().findCourse("COSC1011") != nullptr);                         // upper-cased codes
    CHECK_THROWS(f.legacy.import(f.admin, f.dir.path / "missing.dat"), NotFoundException);
    CHECK(!f.backups.list(f.admin).empty());                                        // pre-import snapshot
}

// ================================================================== UI layer
TEST(input_validator_never_loops_on_bad_numbers) {
    UiRig rig("abc\n-5\n99999999999999999999\n1e3\n  \n4 2\n42\n");
    CHECK(rig.v.integer("Number", 0, 100) == 42);
    UiRig rig2("x\nnan\n12.5.5\n\n98.5\n");
    CHECK(rig2.v.number("Mark", 0, 100) == 98.5);
    UiRig eof("abc\nstill-bad\n");
    CHECK_THROWS(eof.v.integer("Number", 0, 100), InputClosedException);              // EOF ends the loop
    UiRig empty("");
    CHECK_THROWS(empty.v.line("Name", 20), InputClosedException);
    std::string flood;
    for (int i = 0; i < 30; ++i) flood += "zzz\n";
    UiRig spam(flood);
    CHECK_THROWS(spam.v.integer("Number", 0, 100), ValidationException);              // attempt cap
}

TEST(input_validator_text_hardening) {
    UiRig longLine(std::string(5000, 'a') + "\nok\n");
    CHECK(longLine.v.line("Field", 10) == "ok");                                      // overflow rejected, no crash
    UiRig ctrl(std::string("ab\x01" "c\nfine\n"));
    CHECK(ctrl.v.line("Field", 20) == "fine");
    UiRig badUtf("\xff\xfe\nvalid\n");
    CHECK(badUtf.v.line("Field", 20) == "valid");
    UiRig cancel("/cancel\n");
    CHECK_THROWS(cancel.v.line("Field", 20), OperationCancelled);
    UiRig cancel2("  /CANCEL \n");
    CHECK_THROWS(cancel2.v.integer("N", 0, 5), OperationCancelled);
    UiRig menuCancel("/cancel\nx\n");
    CHECK(menuCancel.v.menuKey().empty());                                            // not fatal at a menu prompt
    CHECK(menuCancel.v.menuKey() == "x");
    UiRig optional("\n");
    CHECK(optional.v.line("Field", 20, true).empty());
    UiRig deflt("\n");
    CHECK(deflt.v.line("Field", 20, false, nullptr, "keep") == "keep");
    UiRig checker("bad\ngood\n");
    CHECK(checker.v.line("Field", 20, false, [](const std::string& s) -> std::optional<std::string> {
        return s == "good" ? std::nullopt : std::optional<std::string>("nope");
    }) == "good");
    UiRig yn("maybe\nY\n");
    CHECK(yn.v.confirm("Sure?"));
    UiRig no("\n");
    CHECK(!no.v.confirm("Sure?"));
    UiRig typed("delete\n");
    CHECK(!typed.v.confirmTyped("Danger", "DELETE"));                                 // exact, case-sensitive
    UiRig typedOk("DELETE\n");
    CHECK(typedOk.v.confirmTyped("Danger", "DELETE"));
}

TEST(stream_failure_state_is_recovered) {
    std::istringstream in("first\nsecond\n");
    in.setstate(std::ios::failbit);                                                   // simulated bad state
    bool truncated = false;
    // cin.clear() + cin.ignore(): the failure flag is reset and the damaged line is discarded.
    CHECK(ui::readBoundedLine(in, 10, truncated).empty() && truncated);
    CHECK(in.good());
    CHECK(ui::readBoundedLine(in, 10, truncated) == "second");                        // reading continues normally
    CHECK_THROWS(ui::readBoundedLine(in, 10, truncated), InputClosedException);       // genuine EOF still throws
    std::istringstream broken("data\n");
    broken.setstate(std::ios::badbit);
    CHECK_THROWS(ui::readBoundedLine(broken, 10, truncated), InputClosedException);   // unrecoverable -> no spin
    std::istringstream in2("abc");                                                    // no trailing newline
    CHECK(ui::readBoundedLine(in2, 10, truncated) == "abc");
    std::istringstream in3("a\r\nb\n");
    CHECK(ui::readBoundedLine(in3, 10, truncated) == "a");                            // CRLF handled
    std::istringstream in4(std::string(100, 'x') + "\nnext\n");
    CHECK(ui::readBoundedLine(in4, 10, truncated).size() == 10 && truncated);
    CHECK(ui::readBoundedLine(in4, 10, truncated) == "next" && !truncated);           // rest of long line discarded
}

TEST(password_prompt_is_masked_when_piped) {
    UiRig rig("Secret123\n");
    const std::string pw = rig.v.secret("Password");
    CHECK(pw == "Secret123");
    CHECK(rig.out.str().find("Secret123") == std::string::npos);
    CHECK(rig.out.str().find("********") != std::string::npos);
    UiRig mismatch("Passw0rdA\nPassw0rdB\nPassw0rdA\nPassw0rdA\n");
    CHECK(mismatch.v.newSecret("New password") == "Passw0rdA");
}

TEST(table_builder_alignment_and_width) {
    ui::TableTheme theme;
    ui::TableBuilder t(theme);
    t.title("Students").column({"ID", ui::Align::Left, 4, 0}).column({"Name", ui::Align::Left, 6, 0}).column({"GPA", ui::Align::Right, 3, 0});
    t.row(std::vector<std::string>{"NSE/1", "Abel", "3.50"});
    t.row(std::vector<std::string>{"NSE/2", "\xE6\xBC\xA2\xE5\xAD\x97\xE5\x90\x8D very long name that should be truncated", "4.00"});
    t.row(std::vector<std::string>{"X", "caf\xC3\xA9", "0"});
    t.footer("3 records");
    t.maxTotalWidth(40);
    const std::string out = t.render();
    const auto lines = text::split(out, '\n');
    std::size_t width = 0;
    for (const auto& l : lines) {
        if (l.empty()) continue;
        if (width == 0) width = text::displayWidth(l);
        CHECK(text::displayWidth(l) == width);                                        // perfectly aligned
    }
    CHECK(width <= 40);
    CHECK(out.find("\xE2\x94\x8C") != std::string::npos && out.find("\xE2\x94\x94") != std::string::npos);  // box chars
    ui::TableTheme ascii;
    ascii.box = ui::BoxStyle::ascii();
    ui::TableBuilder a(ascii);
    a.column({"A", ui::Align::Left, 0, 0});
    CHECK(a.render().find("(no records)") != std::string::npos);
    CHECK(a.render().find("+-") != std::string::npos);
}

// ------------------------------------------------------------------ runner
int main() {
    int failedTests = 0;
    for (const auto& t : registry()) {
        const int before = g_failures;
        try {
            t.fn();
        } catch (const std::exception& e) {
            ++g_failures;
            std::cerr << "  EXCEPTION in " << t.name << ": " << e.what() << "\n";
        }
        const bool ok = g_failures == before;
        if (!ok) ++failedTests;
        std::cout << (ok ? "[ PASS ] " : "[ FAIL ] ") << t.name << "\n";
    }
    std::cout << "\n" << registry().size() - static_cast<std::size_t>(failedTests) << "/" << registry().size()
              << " tests passed, " << g_checks << " assertions, " << g_failures << " failed\n";
    return g_failures == 0 ? 0 : 1;
}
