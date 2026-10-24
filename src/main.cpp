// Student Management System v2.0 - entry point.
//   sms [--data-dir DIR] [--ascii] [--no-color] [--help] [--version]
#include <filesystem>
#include <iostream>
#include <string>

#include "sms/core/Exceptions.hpp"
#include "sms/core/FileSystem.hpp"
#include "sms/core/Time.hpp"
#include "sms/services/AccessPolicy.hpp"
#include "sms/storage/DataStore.hpp"
#include "sms/ui/AppContext.hpp"
#include "sms/ui/InputValidator.hpp"
#include "sms/ui/MenuController.hpp"
#include "sms/ui/TerminalRenderer.hpp"

namespace fs = std::filesystem;
using namespace sms;

namespace {

constexpr const char* kVersion = "2.0.0";

void printUsage() {
    std::cout << "Student Management System " << kVersion << "\n\n"
              << "Usage: sms [options]\n"
              << "  --data-dir DIR   directory for the database, backups, exports and logs (default: ./sms_data)\n"
              << "  --ascii          use plain ASCII borders instead of Unicode box drawing\n"
              << "  --no-color       disable ANSI colours (also honours the NO_COLOR variable)\n"
              << "  --version        print the version and exit\n"
              << "  --help           show this help\n";
}

/// Handles a database that exists but cannot be loaded. Returns false if the program must exit.
bool recoverFromCorruption(ui::TerminalRenderer& ui, ui::InputValidator& in, storage::DataStore& store,
                           BackupService& backups, storage::AuditLogger& audit, const DatabaseException& error) {
    ui.beginScreen({"Database problem"}, nullptr);
    ui.error("The database could not be loaded.");
    ui.line("   " + std::string(error.what()));
    audit.critical("DB_LOAD_FAILED", "system", error.what());
    ui.line();
    ui.menu("Recovery options", {{"1", "Restore newest valid backup", "Recommended - the damaged file is kept aside"},
                                 {"2", "Start with an empty database", "Damaged file is renamed, not deleted"},
                                 {"0", "Exit", "Investigate manually"}});
    while (true) {
        const std::string k = in.menuKey();
        const fs::path file = store.repository().file();
        auto quarantine = [&]() {
            const fs::path aside = file.string() + ".corrupt-" + timeutil::fileStamp();
            std::error_code ec;
            fs::rename(file, aside, ec);
            if (ec) throw DatabaseException("Could not move the damaged file aside: " + ec.message());
            audit.critical("DB_QUARANTINED", "system", "moved to " + aside.filename().string());
            ui.info("Damaged file kept as " + aside.string());
        };
        if (k == "1") {
            quarantine();
            const std::string restored = backups.recoverLatestValid("corrupt-database");
            if (restored.empty()) {
                ui.error("No valid backup was found. Starting with an empty database.");
                store.load();
            } else {
                ui.success("Restored from " + restored);
            }
            return true;
        }
        if (k == "2") {
            quarantine();
            store.load();
            return true;
        }
        if (k == "0") return false;
        ui.fieldError("Choose 1, 2 or 0.");
    }
}

}  // namespace

int main(int argc, char** argv) {
    ui::TerminalOptions options;
    fs::path dataDir = "sms_data";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--version") {
            std::cout << "sms " << kVersion << "\n";
            return 0;
        }
        if (arg == "--ascii") {
            options.forceAscii = true;
        } else if (arg == "--no-color") {
            options.forceNoColor = true;
        } else if (arg == "--data-dir" && i + 1 < argc) {
            dataDir = argv[++i];
        } else {
            std::cerr << "Unknown or incomplete option: " << arg << "\n\n";
            printUsage();
            return 2;
        }
    }

    try {
        fsutil::ensureDirectory(dataDir);
        fsutil::InstanceLock lock(dataDir / ".sms.lock");  // released automatically on any exit path

        ui::Terminal terminal(options, std::cin, std::cout);
        ui::TerminalRenderer renderer(terminal);
        ui::InputValidator input(terminal, renderer);

        storage::AuditLogger audit(dataDir / "security_audit.log");
        storage::DataStore store{storage::FileRepository(dataDir / "sms.db")};
        BackupService backups(store, audit, dataDir / "backups");
        AuthService auth(store, audit);
        StudentManager students(store, audit);
        CourseManager courses(store, audit);
        EnrollmentManager enrollments(store, audit);
        AnalyticsEngine analytics(store, audit);
        ExportService exports(store, audit, dataDir / "exports");
        LegacyImporter legacy(store, audit, backups);

        try {
            store.load();
        } catch (const DatabaseException& e) {
            if (!recoverFromCorruption(renderer, input, store, backups, audit, e)) return 1;
        }
        backups.snapshot("system", "startup");
        audit.info("APP_START", "system", std::string("version ") + kVersion);

        ui::AppContext ctx{terminal, renderer, input, store, audit, auth, students, courses,
                           enrollments, analytics, exports, backups, legacy, std::nullopt, {}};
        ui::MenuController controller(ctx);
        const int code = controller.run();
        audit.info("APP_STOP", "system", "normal exit");
        return code;
    } catch (const InputClosedException&) {
        return 0;
    } catch (const SmsException& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected fatal error: " << e.what() << "\n";
        return 1;
    }
}
