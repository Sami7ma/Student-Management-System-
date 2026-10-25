<p align="center">
  <img src="docs/img/SMS.png" alt="Student Management System — C++17 terminal interface" width="100%">
</p>

<h1 align="center">Student Management System</h1>

<p align="center">
  A local-first student records application built in C++17, with a role-aware terminal interface,
  academic analytics, and explicit storage and recovery rules.
</p>

<p align="center">
  <strong>C++17</strong> · <strong>Standard library only</strong> · <strong>Make &amp; CMake</strong> ·
  <strong>Unit &amp; end-to-end tests</strong>
</p>

This project replaces an earlier single-file `main.cpp` with a layered application. Terminal screens,
application services, storage, domain models, and core utilities have separate responsibilities. The
result is easier to test and extend, while keeping the application usable from a terminal without
third-party runtime dependencies.

## Screenshots

| Dashboard and system status | Analytics and grade distribution |
| --- | --- |
| ![Dashboard](docs/img/SMS%202.png) | ![Analytics overview](docs/img/SMS3.png) |

<p align="center">
  <img src="docs/img/SMS%204.png" alt="Interactive analytics and reports menu" width="88%">
</p>

[Watch the terminal walkthrough](docs/img/SMS.webm)

## At a glance

- **Role-based access:** Admin, Faculty, and Student permissions are enforced in the service layer.
- **Academic workflows:** Student and course records, enrollments, grades, transcripts, and search.
- **Terminal UI:** Colour, Unicode tables, responsive column widths, breadcrumbs, status information, and pagination.
- **Integrity and recovery:** Validated records, transactional file updates, backups, and a hash-chained audit log.
- **Reports:** GPA and course analytics, CSV and Markdown exports, and printable text transcripts.
- **Legacy migration:** An importer for the previous `students.dat` format.

## Interface and interaction design

The interface is designed for terminal use rather than a graphical desktop. Screens share a consistent layout and input flow, so navigation, validation, and destructive actions behave predictably.

- **Visual hierarchy:** ANSI colour, cyan borders, gold headings, and restrained green, amber, and red status indicators.
- **Readable tables:** Unicode box drawing, widths adapted to the terminal and content, and UTF-8 display-width handling for non-Latin names.
- **Navigation context:** Breadcrumbs and a status bar showing the signed-in user, role, audit status, time, and database state.
- **Safer actions:** Confirmation prompts for destructive operations and typed confirmations such as `DELETE` and `RESTORE` for irreversible actions.
- **Accessible fallbacks:** `--ascii` for plain borders, `--no-color` to disable ANSI colours, and support for the `NO_COLOR` environment variable.
- **Consistent forms:** A shared prompt reader, input validation, a `/cancel` option, and explicit handling of end-of-input.

## Features

### Accounts and access control

- Roles: **Admin**, **Faculty**, and **Student**.
- Permissions are checked by `AccessPolicy` in the service layer rather than relying on menu visibility alone.
- Students can view only their own profile and transcript.
- First-run setup creates the administrator account; there is no built-in default password.
- Password prompts are masked. Stored credentials use PBKDF2-HMAC-SHA-256 with a per-user salt and constant-time hash comparison.
- Five consecutive failed attempts lock a username for five minutes. Unknown usernames follow a comparable password-verification path and receive the same public error as an incorrect password.
- Security events are appended to `security_audit.log`. Each entry participates in a SHA-256 hash chain, which the application can verify to identify the first inconsistent line.

### Student, course, and academic records

- Student records: create, view, edit, deactivate, restore, and permanently delete.
- Courses: create, edit, retire, reactivate, and permanently delete.
- Enrollments: enroll and withdraw students.
- Grades: record, overwrite, and clear grades.
- Search by partial name, ID, or department; filter by GPA; sort by name, ID, GPA, enrollment date, or department in either direction.
- Pagination for lists and confirmation before destructive operations.
- Validation is applied at the UI, service, and database-loading boundaries so invalid data is not accepted just because it bypasses a form.

### Analytics and reporting

- Credit-weighted GPA and CGPA, including per-term GPA.
- Academic standing: Dean's List at GPA 3.50 or higher, Good Standing at 2.00 or higher, and Probation below 2.00.
- Course mean, median, standard deviation, minimum, maximum, pass/fail rate, and grade distribution.
- Department summaries and top-student reports.
- Printable transcripts and exports to CSV, Markdown, and plain-text transcript files.
- CSV output follows RFC 4180 conventions, includes a UTF-8 BOM, and prefixes spreadsheet-formula-leading values to reduce formula-injection risk.

### Input validation and error handling

- Validation covers student IDs, names, emails, phone numbers, departments, real calendar dates, age, credits, ECTS, marks, GPA ranges, terms, usernames, and passwords.
- Numeric input must match the complete value; inputs such as `nan`, `inf`, `12abc`, and out-of-range exponents are rejected.
- A bounded input reader handles stream errors, end-of-input, control characters, and UTF-8 checks, with a limit on retries for an individual field.
- Domain errors use explicit exception types, including validation, authentication, database, and not-found errors.
- Resource cleanup uses RAII for files, locks, and terminal state. The application code does not use manual `new` / `delete` allocation.

### Persistence, backups, and recovery

- The custom `SMSDB v2` format contains a magic header, escaped records, a record count, and a SHA-256 integrity value.
- Mutations operate on a copy and are persisted before the in-memory state is committed; failed validation or write operations can be rolled back.
- Writes use a temporary file, `fsync`, and atomic rename where supported by the platform.
- Backups are created at startup, before permanent deletion, import, and restore operations, and on demand. The newest 20 snapshots are retained.
- A snapshot is validated before restore. Restore rejects path traversal, and startup recovery can offer a valid backup if the database is damaged while preserving the damaged file.
- A cross-process lock helps prevent two application instances from writing to the same data directory at once.

## Quick start

### Requirements

- A C++17-compatible compiler
- GNU Make for the Make workflow, or CMake for the alternative workflow
- No third-party runtime libraries

### Build and run with Make

```sh
make
./build/sms
```

On first launch, create an administrator account and then sign in. By default, application data is
stored in `./sms_data`. Use `--data-dir DIR` to choose another directory:

```sh
./build/sms --data-dir "$HOME/.local/share/sms"
```

To import an older `students.dat` file, sign in as an administrator and open **Administration → Import legacy students.dat**. A synthetic sample fixture is provided in `examples/legacy_students.dat` for testing and demonstrating the importer.

### Build with CMake

```sh
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake
ctest --test-dir build-cmake --output-on-failure
```

### Command-line options

| Option | Description |
| --- | --- |
| `--data-dir DIR` | Store runtime files in the specified directory. |
| `--ascii` | Use ASCII table borders instead of Unicode box drawing. |
| `--no-color` | Disable ANSI colour output; also respects `NO_COLOR`. |
| `--version` | Print version information. |
| `--help` | Show command-line help. |

Enter `/cancel` at a form prompt to cancel the current form.

## Build and test

```sh
make                   # Release build: build/sms
make test              # Unit tests
make debug             # AddressSanitizer + UBSan build and test run
bash tests/e2e_smoke.sh # End-to-end checks against the built executable
```

| Test / Build Target | Status |
| --- | --- |
| Strict-warning Make build (`-Wall -Wextra -Wpedantic -Werror`) | Passed |
| Unit test suite (`make test`) | 34 tests, 375 assertions — passed |
| End-to-end smoke test suite (`bash tests/e2e_smoke.sh`) | 38 checks — passed |
| CMake Release build and CTest | Passed (1 CTest target) |
| AddressSanitizer + UBSan build (`make debug`) | Supported |
| Platform coverage | Verified on Linux; POSIX compliant |

Verification was performed on Linux with GCC (`-std=c++17`).

## Architecture

The dependency direction is intentionally one-way:

```text
UI → Services → Storage → Models → Core
```

The UI owns terminal interaction. Services implement workflows and enforce permissions. Storage handles file access, serialization, transactions, backups, and audit logging. Models represent academic entities and database integrity, while core utilities provide shared cryptography, text handling, time, filesystem operations, and exceptions.

| Location | Responsibility |
| --- | --- |
| `include/sms/` | Public headers organised by application layer. |
| `src/core/` | Cryptographic primitives, text and UTF-8 handling, time, filesystem support, and exceptions. |
| `src/models/` | Student, course, enrollment, grading, validation rules, and database aggregate. |
| `src/services/` | Access policy, authentication, academic workflows, analytics, import/export, and backups. |
| `src/storage/` | Serialization, repository access, transactions, and audit logging. |
| `src/ui/` | Terminal rendering, prompts, validation, navigation, and screens. |
| `tests/test_main.cpp` | Unit tests. |
| `tests/e2e_smoke.sh` | Scripted end-to-end checks. |
| `docs/AUDIT_REPORT.md` | Audit of the original single-file implementation and issues addressed by the rewrite. |
| `docs/TEST_CHECKLIST.md` | Manual edge-case checklist. |

The runtime data directory contains `sms.db`, `security_audit.log`, `backups/`, `exports/`, and `.sms.lock`.

## Design decisions and limitations

This is a local terminal application and a software project for learning and demonstration. It has not been independently security-audited or certified for handling real institutional records. The current trade-offs are documented here deliberately.

- **Password hashing parameters need review before real-world use.** The current implementation uses PBKDF2-HMAC-SHA-256 with 100,000 iterations and a 16-byte per-user salt. OWASP currently lists 600,000 iterations for PBKDF2-HMAC-SHA-256 when PBKDF2 is required, and recommends Argon2id for many new systems. Increasing the work factor should be benchmarked and paired with a migration plan for existing hashes. See the [OWASP Password Storage Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html).
- **Data at rest is not encrypted.** POSIX file permissions are restricted, but anyone able to read the database file can read the academic records.
- **The audit log is tamper-evident, not tamper-proof.** Someone who can rewrite the entire file and recompute the chain can forge a consistent history. Stronger guarantees require an independently stored checkpoint or controlled append-only log storage.
- **Login lockout state is held in memory.** Restarting the application clears the failed-login counters. The idle timeout is checked when the next menu choice arrives; it cannot interrupt a prompt that is waiting for input.
- **The file store rewrites the database on each mutation.** Copy-before-write keeps the transaction model straightforward, but the approach is intended for modest data sets. SQLite would be a reasonable next storage backend if record counts or concurrency requirements grow.
- **Platform-specific code:** Windows terminal input (`_getwch`), virtual-terminal handling, and file locking are implemented, but development and automated testing have primarily targeted Linux environments.
- **The CSV formula guard alters some values intentionally.** Values beginning with `=`, `+`, `-`, or `@` are prefixed with an apostrophe to reduce spreadsheet formula execution risk; this includes phone numbers beginning with `+`.

### GPA and legacy import policy

GPA is credit-weighted, rounded to two decimal places, and uses the project's original letter-grade scale. Every graded enrollment attempt counts toward the calculation, so a retake is treated as a separate attempt. The pass mark is 40. The sample legacy students' GPAs were checked against the original algorithm.

The legacy importer only has letter grades available, so each imported letter grade is represented by the midpoint of its percentage band. This preserves the letter-grade-based GPA calculation, but the original exact percentage cannot be recovered. Imported terms are labelled `LEGACY`. The original single-file program also had a first-course-dropping bug; if an old `students.dat` file was already loaded and saved by that program, the lost course records cannot be recovered by this importer.

The SHA-256, HMAC, and PBKDF2 implementations in `src/core/Crypto.cpp` are checked against published known-answer test vectors. That is useful test coverage, but it does not replace an independent cryptographic review or a maintained cryptography library for security-sensitive deployments.

## License

This project is licensed under the [MIT License](LICENSE). See the [LICENSE](LICENSE) file for details.
