# Phase 1 - Audit Report: original `main.cpp` (434 lines)

**Scope note.** The uploaded archive contained exactly two files: `main.cpp` and `students.dat`.
It did **not** contain assignment specifications, so "assignment compliance" below is measured
against (a) what the program's own menu promises and (b) the feature list in your refactoring
brief. If you have a separate assignment document, check it against the feature table in the README.

Every defect marked **[verified]** was reproduced by compiling and running your original program.

---

## 1. Compliance / functional gaps (vs. the brief)

| Requirement | Original | Status |
|---|---|---|
| Create / Read / Update / Delete students | add, search, edit *grades only*, hard delete | Partial |
| Update name / phone / major | impossible (`editStudent`, L291-320 only re-enters every mark) | Missing |
| Edit or remove a single course | no | Missing |
| Courses as first-class entities / catalogue | course data duplicated inside every student | Missing |
| Soft delete / restore | no | Missing |
| Authentication, roles, audit log | none | Missing |
| Multi-criteria search, filter, sort | exact, case-sensitive ID-or-name match (L161); sort is name-only at insert time | Missing |
| GPA range filter, department filter | no | Missing |
| Analytics (CGPA, standing, course stats) | GPA only; no standing, no mean/median/pass-rate | Missing |
| Transcript / CSV / Markdown export | no | Missing |
| Backups | no | Missing |
| Input validation | none (IDs, phone, marks, credits, name) | Missing |
| Atomic persistence + integrity check | no | Missing |
| Layered architecture / build system | one file, no build file | Missing |

## 2. Data-integrity defects (the serious ones)

1. **[verified] Silent permanent data loss on every load - `loadFromFile`, L115.**
   `file.ignore()` runs right after the phone `getline`, which already consumed the newline, so
   it throws away the first character of the next line (`COURSE` -> `OURSE`). The loop at L120
   then never recognises that course marker, so **the first course of every student is dropped**.
   Because `saveToFile()` rewrites the file, the loss becomes permanent:
   * Abel Tesfaye loads with 7 courses; GPA shows `3.57143` instead of the true `3.625`.
   * `students.dat` shrank from **80 to 70 course records** after one start/exit cycle;
     "Programming I" was gone from the file.
   * A student with *no* courses loses the `E` of `ENDSTUDENT`, so the loop (L118) never
     terminates the block and swallows the following student(s) into it.
2. **Non-atomic save - `saveToFile`, L71.** `ofstream file("students.dat")` truncates the file
   immediately and writes with no error check. A crash, power loss, full disk or Ctrl-C mid-write
   destroys the whole database. Nothing is flushed/verified; no backup exists.
3. **Format injection - L106/L119/L120.** The format uses bare words as delimiters. A student
   named `STUDENT` or a course titled `ENDSTUDENT` corrupts the file structure on next load.
4. **No integrity or validity checks on load** (L98-152): no checksum, no version, truncated or
   hand-edited files are accepted partially; numeric fields are read with `>>` and unchecked
   (L124-125), and L126 `ignore()` is another fragile blind skip.
5. **No uniqueness:** `addStudent` (L172) never checks the ID, so duplicates are possible;
   `searchStudent`/`deleteStudent` then act on whichever comes first.

## 3. Code smells and anti-patterns

* `using namespace std;` (L8) - namespace pollution.
* Global mutable state: `Student* head` (L36).
* Hand-rolled singly linked lists with raw owning pointers (`Course*`, `Student*`, L14-31) and
  O(n) append (L362-365).
* **Memory leaks:** `new` at L107, L121, L168, L187, L345 with **no `delete` anywhere**. Deleting a
  student (L254-263) unlinks the node but never frees the `Student` or its `Course` chain; normal
  exit (L430) frees nothing. No RAII, no destructors, not exception-safe.
* Uninitialised fields: `new Student`/`new Course` leave `gpa`, `credit`, `ects` indeterminate
  until assigned (L107, L121, L168, L187, L345).
* `Course` carries grade *letters* only (L19) - the percentage is discarded, so grades cannot be
  recomputed, averaged, or audited.
* `float` for GPA/credits (L17, L28): precision loss and ragged output (`3.57143`) - no formatting.
* Monolithic UI/logic/storage mixing: every function does prompting, business rules, mutation and
  `saveToFile()` in one body (L167-229, L334-373).
* Duplicated code: the sorted-insert block is copy-pasted (L138-148 and L216-225); the course
  prompt block is copy-pasted (L186-212 and L345-357).
* Forward declarations out of order (L32-33), magic strings for grade letters, `NULL` instead of
  `nullptr`, `endl` flushing, no `const` anywhere.
* `saveToFile()` called after every mutation *and* at exit: more write amplification and more
  exposure to defect 2.

## 4. Crash / vulnerability vectors

| Where | Problem |
|---|---|
| L411 `cin >> ch` | Non-numeric input puts `cin` in a fail state; `ch` becomes 0, which is **"Save & Exit"** (L430) - one typo quits the program. The fail flag is never cleared, so every later `cin >>` also fails. |
| L182 `cin >> n` (then L186 loop) | Letters make `n` 0 -> **[verified]** prints "Student added." with no courses. A huge `n` runs a loop of that size, allocating a `Course` per pass with no upper bound -> memory exhaustion. |
| L193, L195, L207, L309, L348-349, L354 | Same unchecked `cin >> float`: after one bad token every following read silently fails, so grades/credits are recorded as 0 and the form completes with garbage. |
| L207, L309, L354 | Marks are not range-checked (negative, > 100, NaN accepted). |
| L193, L348 | Credit hours unchecked: 0 or negative credits corrupt the GPA (L58-63; the zero-guard only covers `cr == 0`). |
| L172-179 | Free-form strings (ID, name, phone, major) have no length cap, charset check or control-character check. |
| L71 / L99 | File I/O never checked (`file.close()` result ignored, write errors ignored, missing-file handled only by silent return). |
| (no locking) | Two running instances overwrite each other's data - classic lost-update race. |
| L161 | Authorisation: none - anyone with the binary can read/modify/delete everything. |
| Buffer overflows | **None found:** the program uses `std::string`, not `gets`/`strcpy`/`sprintf`/`scanf`. This was already a strength; the rewrite keeps it and adds hard input-length caps. |

## 5. How the rewrite addresses each finding

| Finding | Resolution in v2 |
|---|---|
| Load drops first course / swallows students | Unambiguous record format; each record is one escaped line; no `ignore()` games. Tested with round-trip and corruption tests. |
| Non-atomic save, no backup | `tmp` file + `fsync` + atomic `rename`; startup snapshot + snapshot before every destructive/import action; validated restore. |
| Format injection / unchecked load | Field escaping (`\\ \t \n \r`), SHA-256 checksum trailer, schema + referential-integrity validation on startup, descriptive errors with line numbers. |
| Leaks / raw pointers | Zero `new`/`delete`; value types and STL containers; RAII for files, locks and terminal state. |
| Global state | Dependency-injected services; no globals. |
| Failed `cin` states, huge loops | `InputValidator`: bounded `get()`-based line reader, strict parsers, attempt cap, EOF detection. |
| No auth / audit | RBAC (Admin/Faculty/Student) enforced in the service layer, PBKDF2 password hashing, lockout, tamper-evident audit log. |
| Race between instances | Cross-process exclusive lock file (`flock` / exclusive `CreateFile`). |
| Missing features | See README feature table. |
