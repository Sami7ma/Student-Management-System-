# Verification & Test Checklist

## A. Compile
```sh
# Make (recommended)
make                       # g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror
# Direct compiler invocation
mkdir -p build && g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Werror -Iinclude \
    $(find src -name '*.cpp') -o build/sms
# CMake
cmake -S . -B build-cmake && cmake --build build-cmake
# Clang also works:  make CXX=clang++
```
**Expected:** no warnings, binary at `build/sms`.

## B. Automated suites
```sh
make test                  # 34 unit tests / 375 assertions
make debug                 # same under AddressSanitizer + UBSan (clean rebuild, then `make` to return to release)
tests/e2e_smoke.sh         # 38 end-to-end checks
```

## C. Manual checklist (run `./build/sms --data-dir /tmp/sms_manual`)
Use `--ascii` if your terminal cannot show box-drawing characters.

| # | Scenario | Steps | Expected |
|---|---|---|---|
| 1 | **Empty database / first run** | Start with a new empty `--data-dir` | "First-run setup" screen; weak password (`abc`) is refused with a reason; mismatched confirmation is refused; after success you land on Sign in. Dashboard shows `Students 0 Courses 0`. |
| 2 | Empty lists | Students -> Browse | Table shows `(no records)`, no crash. Courses -> Catalogue, Analytics -> Overview likewise. |
| 3 | **Wrong credentials** | Sign in with a wrong password | Generic "Invalid username or password."; same message for an unknown username. |
| 4 | **Brute force** | 5 wrong passwords for one user, then the right one | "Too many failed attempts. Try again in N seconds." even for the correct password. Audit log shows `LOGIN_FAILED` x5 then `LOGIN_LOCKED`. |
| 5 | **Masked password** | Type a password, use Backspace | Only `*` shown; backspace removes one; nothing readable on screen or in scrollback. |
| 6 | **Letters in numeric prompts** | Enrollments -> Record grade -> at *Mark* type `abc`, `-5`, `150`, `1e999`, `nan`, then `88.5` | Each bad value gets a red message and a re-prompt; no loop, no crash; `88.5` accepted. |
| 7 | **Letters in menus** | At any menu type `abc`, `99999999999`, an empty line | "Unknown option" / nothing happens; program keeps running. |
| 8 | **Overlong / hostile input** | Paste 5000 characters, a control character, or invalid UTF-8 into a name field | "Input too long" / "control characters" / "invalid text encoding"; re-prompt. |
| 9 | **EOF** | `./build/sms < /dev/null` or press Ctrl-D at a prompt | Clean exit, no infinite loop. |
| 10 | Cancel | Start *Add student*, type `/cancel` | "Cancelled - nothing was changed." |
| 11 | **Domain validation** | Add student with IDs `abc`, duplicate ID, name `Bad123`, email `bad@`, phone `abc`, DOB `2030-01-01`, DOB `2003-02-30`, age 5 | Each rejected with a specific message. |
| 12 | **Unauthorized access** | Admin creates a Faculty and a Student account (linked to a student). Sign in as Faculty -> main menu `6`, Students -> Deactivate | `Access denied: your role (Faculty) may not ...`. Sign in as Student: only the student portal is offered; the audit log shows `ACCESS_DENIED` lines. |
| 13 | Student isolation | As Student, try to view another ID (not possible in the menu; covered by unit test `rbac_matrix_enforced_in_services`) | Service refuses with "you may only view your own record". |
| 14 | Forced password change | First login of an admin-created account | Must choose a new password before the dashboard appears. |
| 15 | **Destructive confirmations** | Deactivate a student; Recycle bin -> Hard delete | Deactivate asks y/N. Hard delete requires typing `DELETE`; a snapshot is taken first (check *Backups*). |
| 16 | Edit with diff | Students -> Edit, change one field | Shows `old -> new`, asks before overwriting; answering `n` discards. |
| 17 | **Corrupt database** | Stop the program; flip a byte in `sms.db`; start again | "Database checksum mismatch"; recovery menu. Option 1 restores the newest valid backup and keeps `sms.db.corrupt-<time>` aside. |
| 18 | Truncated database | `truncate -s -20 sms.db`, start | "truncated" / "END trailer" error and the same recovery menu. |
| 19 | Crash safety | `kill -9` the process while idle at a menu, restart | Data intact. A leftover `sms.db.tmp` (if any) is ignored and replaced on the next save. |
| 20 | Second instance | Start a second copy on the same `--data-dir` | "Another instance is already using this data directory", exit code 1. |
| 21 | Audit tamper detection | Edit any line of `security_audit.log`, then Administration -> Security audit log | "TAMPERING DETECTED ... breaks at line N". |
| 22 | Legacy import | Administration -> Import legacy -> `examples/legacy_students.dat` | 10 students, 80 courses, 80 enrollments, 0 warnings; GPAs 3.50-3.63. Importing the same file twice skips all students with warnings. |
| 23 | Search / sort / pagination | Add > 15 students; Browse; `N`/`P`/`G`; `S` to change sort; Search with GPA range 3.5-4.0 | Pages navigate; sort order changes; GPA filter obeys the range; min > max is rejected. |
| 24 | Exports | Browse -> `X` and `M`; Reports menu | Files appear in `exports/`; CSV opens in Excel with correct accents; never overwrites an earlier export. |
| 25 | Backup restore | Admin -> Backups -> Create, change data, Restore (type `RESTORE`) | Data returns to the snapshot; current state saved first as `pre-restore`. |
| 26 | Non-colour / narrow terminals | `NO_COLOR=1 ./build/sms --ascii`; resize to 70 columns | Plain ASCII output; wide tables shrink long columns with `~`/`...`. |
