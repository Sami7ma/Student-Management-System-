# Student Management System

<p align="center">
  <img src="docs/img/SMS.png" alt="Student Management System v2.0" width="100%" />
</p>

A local, role-based student management application built in **C++17**. This project is a full rewrite of an earlier single-file program, organized into separate domain models, services, persistence, core utilities, and terminal UI. Its focus is reliable academic-record handling, explicit access rules, input validation, and recoverable file storage.

**C++17 · No third-party runtime dependencies · Make and CMake · Unit and end-to-end tests**

## Screenshots

| Dashboard | Analytics overview |
| --- | --- |
| ![Dashboard](docs/img/SMS%202.png) | ![Analytics overview](docs/img/SMS3.png) |

![Analytics and reports menu](docs/img/SMS%204.png)

[Open the terminal walkthrough](docs/img/SMS.webm)

## Features

### Access and accounts

- Three roles: **Admin**, **Faculty**, and **Student**.
- Role checks live in the service layer, not only in menus. Students can access only their own profile and transcript.
- First-run administrator setup; no default administrator password.
- Passwords are stored as PBKDF2-HMAC-SHA-256 hashes with per-user salts. Password prompts mask input, and repeated failed logins trigger a temporary lockout.
- Security events are recorded in a hash-chained audit log. The application can verify the chain and report where a modified log first becomes inconsistent.

### Academic records

- Student create, view, edit, deactivate, restore, and permanent-delete workflows.
- Course catalogue management, including retirement and reactivation.
- Enrollment, withdrawal, and grade entry, correction, and clearing.
- Case-insensitive partial search, GPA filtering, sorting, and paginated lists.
- Input and domain validation are applied at the UI, service, and database-loading boundaries.

### Analytics and exports

- Credit-weighted GPA and CGPA, term GPA, and academic standing.
- Course statistics: mean, median, standard deviation, minimum, maximum, pass/fail rate, and grade distribution.
- Department summaries, top-student views, and printable transcripts.
- CSV export with spreadsheet-formula-injection mitigation, Markdown reports, and text transcripts.

### Storage and recovery

- Custom `SMSDB v2` file format with escaped records, a record count, and a SHA-256 integrity checksum.
- Mutations are applied to a copy and persisted before being committed in memory, so failed validation or writes can be rolled back.
- Temporary-file writes, `fsync`, and atomic rename are used where supported by the platform.
- Automatic and on-demand snapshots, retention of the newest 20 backups, snapshot validation before restore, and guided recovery when the database is damaged.
- A cross-process lock prevents two instances from writing to the same data directory at the same time.
- RAII is used for terminal state, files, and locks.

### Terminal interface

- ANSI colour, Unicode box tables, breadcrumbs, status information, confirmations for destructive actions, and pagination.
- Table widths adapt to terminal size and UTF-8 display width.
- `--ascii`, `--no-color`, and the `NO_COLOR` environment variable provide simpler terminal output.

## Quick start

### Build with Make

Requirements: a C++17 compiler and GNU Make.

```sh
make
./build/sms
```

On first launch, create an administrator account and then sign in. There is no built-in default password. By default, application data is stored in `./sms_data`; use `--data-dir DIR` to choose another location.

For a separate local data directory:

```sh
./build/sms --data-dir "$HOME/.local/share/sms"
```

To import an older `students.dat` file, sign in as an administrator and open **Administration → Import legacy students.dat**. The repository includes a fixture at `examples/legacy_students.dat` for demonstrating the import path. Ensure the fixture contains only fabricated data before publishing the repository.

### Build with CMake

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

### Command-line options

| Option | Purpose |
| --- | --- |
| `--data-dir DIR` | Use a specific directory for runtime data. |
| `--ascii` | Use plain ASCII table borders. |
| `--no-color` | Disable ANSI colours. Also respects `NO_COLOR`. |
| `--version` | Print version information. |
| `--help` | Show command-line help. |

Enter `/cancel` at a form prompt to cancel that form.

## Build and test

```sh
make                  # Release executable: build/sms
make test             # Unit tests
make debug            # Rebuild and run tests with AddressSanitizer + UBSan
bash tests/e2e_smoke.sh # Scripted end-to-end checks against the real binary
```

The current unit suite contains **34 tests and 375 assertions**. The end-to-end script contains **38 checks**.

**Verification during this README review (Linux, GCC 14.2):** the strict-warning Make release build passed; all 34 unit tests and 375 assertions passed; all 38 end-to-end checks passed; and the CMake Release build plus CTest passed. The sanitizer build was not completed during this review, and Windows/macOS builds have not been verified here.

## Architecture

The dependency direction is:

```text
UI → Services → Storage → Models → Core
```

The UI owns terminal interaction. Services enforce application rules and permissions. Storage handles serialization, file access, transactions, audit logging, and backups. Models represent academic entities and database integrity; core utilities provide shared text, time, cryptography, exceptions, and filesystem operations.

| Path | Responsibility |
| --- | --- |
| `include/sms/` | Public headers grouped by layer. |
| `src/core/` | Cryptography, text handling, time, filesystem support, and exceptions. |
| `src/models/` | Students, courses, enrollments, grading, validation rules, and database aggregate. |
| `src/services/` | Access policy, authentication, academic workflows, analytics, import/export, and backups. |
| `src/storage/` | Serialization, repository access, transactions, and audit logging. |
| `src/ui/` | Terminal rendering, prompts, validation, navigation, and screens. |
| `tests/test_main.cpp` | Unit tests. |
| `tests/e2e_smoke.sh` | Scripted end-to-end checks. |
| `docs/AUDIT_REPORT.md` | Findings from the original single-file implementation and how the rewrite addresses them. |
| `docs/TEST_CHECKLIST.md` | Manual verification scenarios. |

Runtime data in the selected data directory includes `sms.db`, `security_audit.log`, `backups/`, `exports/`, and `.sms.lock`.

## Design notes and limitations

This is a local terminal application and a portfolio project, not a security-certified or independently audited student-record platform. A few implementation details are important to understand before using it with real records:

- **Data at rest is not encrypted.** The database is a custom text format. POSIX file permissions are restricted, but someone who can read the files can read the stored academic information.
- **The audit log is tamper-evident, not tamper-proof.** Someone able to rewrite the entire log can recompute the hash chain. Stronger assurance would require storing checkpoints elsewhere or shipping the log to controlled, append-only storage.
- **Review password-hashing cost before real-world deployment.** The current implementation uses PBKDF2-HMAC-SHA-256 with 100,000 iterations. OWASP currently recommends 600,000 iterations when PBKDF2-HMAC-SHA-256 is required; Argon2id is the preferred option for many new systems. Any change should be benchmarked and include a migration strategy for existing hashes. See the [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html).
- **Lockout state is in memory.** Restarting the application clears failed-login counters. The idle timeout is checked when the next menu choice arrives; it cannot interrupt a blocking prompt.
- **The file store rewrites the full database for each mutation.** This is intentionally simple and suitable for modest data sets, not a high-volume institutional deployment. SQLite or a server database would be a sensible next step if scale or concurrent access requirements grow.
- **Platform coverage is incomplete.** Windows-specific terminal and file-locking branches exist but need first-run testing; Windows and macOS builds are not claimed as verified.

The cryptographic primitives in `src/core/Crypto.cpp` are implemented in the project and tested against published known-answer vectors. That is useful for demonstrating the implementation work, but it is not a substitute for an independent cryptographic review or a maintained cryptography library in a security-sensitive deployment.

## License

This project is distributed under the MIT License; see [`LICENSE`](LICENSE). Confirm that MIT matches your intentions and that you own or have permission to publish all included source code, documentation, screenshots, and video before publishing the repository.
