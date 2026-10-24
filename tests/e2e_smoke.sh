#!/bin/sh
# End-to-end smoke tests against the real binary using scripted (piped) input.
# Usage: tests/e2e_smoke.sh [path-to-sms-binary]      (default: build/sms)
BIN="${1:-build/sms}"
LEGACY="$(cd "$(dirname "$0")/.." && pwd)/examples/legacy_students.dat"
WORK="$(mktemp -d)"
PASS=0; FAIL=0
trap 'rm -rf "$WORK"' EXIT

ok()   { PASS=$((PASS+1)); echo "[ PASS ] $1"; }
bad()  { FAIL=$((FAIL+1)); echo "[ FAIL ] $1"; }
has()  { grep -qF -- "$2" "$1" && ok "$3" || bad "$3  (missing: $2)"; }
hasnt(){ grep -qF -- "$2" "$1" && bad "$3  (found: $2)" || ok "$3"; }

D="$WORK/d1"
printf '%s\n' admin Admin1234 Admin1234 admin Admin1234 \
  6 4 "$LEGACY" y 0 1 1 q 0 0 /cancel | "$BIN" --data-dir "$D" --ascii > "$WORK/s1.out" 2>&1
has   "$WORK/s1.out" "Administrator 'admin' created"        "first-run setup creates the administrator"
has   "$WORK/s1.out" "Students imported    | 10"            "legacy import brings in 10 students"
has   "$WORK/s1.out" "Abel Tesfaye"                          "browse lists imported students"
has   "$WORK/s1.out" "3.63"                                  "GPA 3.63 preserved (original algorithm)"
hasnt "$WORK/s1.out" "Admin1234"                             "password is never echoed"
[ -f "$D/security_audit.log" ] && ok "audit log created" || bad "audit log created"
grep -q "Admin1234" "$D/sms.db" "$D/security_audit.log" && bad "plaintext password on disk" || ok "no plaintext password in db or audit log"
grep -q "pbkdf2-sha256" "$D/sms.db" && ok "password stored as PBKDF2 hash" || bad "password stored as PBKDF2 hash"

# bad credentials + garbage input at every prompt type
printf '%s\n' admin Wrong1234 admin Admin1234 1 4 abc NSE/1001/16 nse/9001/20 Bad123 "Test Person" bad@ \
  tp@example.com abc 0911223344 "Computer Science" 2030-01-01 2003-02-30 2003-02-28 "" y 0 0 0 /cancel \
  | "$BIN" --data-dir "$D" --ascii > "$WORK/s2.out" 2>&1
has "$WORK/s2.out" "Invalid username or password."          "wrong password rejected generically"
has "$WORK/s2.out" "Student ID must look like"               "invalid student ID rejected"
has "$WORK/s2.out" "already exists"                          "duplicate student ID rejected"
has "$WORK/s2.out" "Email address is malformed"              "invalid email rejected"
has "$WORK/s2.out" "Phone number must have"                  "invalid phone rejected"
has "$WORK/s2.out" "Date of birth cannot be in the future"   "future DOB rejected"
has "$WORK/s2.out" "real calendar date"                      "impossible date rejected"
has "$WORK/s2.out" "NSE/9001/20"                             "valid student was created"

# numeric prompts never loop on letters; EOF ends the program
printf '%s\n' admin Admin1234 3 2 NSE/1001/16 abc 999 1 ninety 150 88.5 y 0 0 /cancel \
  | "$BIN" --data-dir "$D" --ascii > "$WORK/s3.out" 2>&1
has "$WORK/s3.out" "Please enter a whole number."            "letters at integer prompt rejected"
has "$WORK/s3.out" "Please enter a number"                   "letters at numeric prompt rejected"
has "$WORK/s3.out" "Value must be between 0 and 100"         "out-of-range mark rejected"
has "$WORK/s3.out" "Recorded 88.50%"                         "valid mark recorded"
printf '%s\n' admin | timeout 10 "$BIN" --data-dir "$D" --ascii > "$WORK/s4.out" 2>&1
[ $? -eq 0 ] && ok "EOF mid-prompt exits cleanly (no spin)" || bad "EOF mid-prompt exits cleanly"

# RBAC: faculty/student cannot do admin things
printf '%s\n' admin Admin1234 6 1 c fac1 2 TempPass1 TempPass1 c stu1 3 NSE/1001/16 TempPass1 TempPass1 q 0 0 0 /cancel \
  | "$BIN" --data-dir "$D" --ascii > /dev/null 2>&1
printf '%s\n' fac1 TempPass1 TempPass1 FacNew1234 FacNew1234 6 1 6 NSE/1002/16 y 0 0 /cancel \
  | "$BIN" --data-dir "$D" --ascii > "$WORK/s5.out" 2>&1
has "$WORK/s5.out" "may not manage user accounts"            "faculty denied user administration"
has "$WORK/s5.out" "may not deactivate students"             "faculty denied deactivation (service-level RBAC)"
printf '%s\n' stu1 TempPass1 TempPass1 StuNew1234 StuNew1234 1 0 /cancel \
  | "$BIN" --data-dir "$D" --ascii > "$WORK/s6.out" 2>&1
has   "$WORK/s6.out" "Student portal"                        "student sees the student portal"
hasnt "$WORK/s6.out" "Administration"                        "student menu hides administration"

# audit trail
for ev in LOGIN_OK LOGIN_FAILED USER_CREATED STUDENT_CREATE GRADE_RECORD ACCESS_DENIED LEGACY_IMPORT; do
  grep -q "$ev" "$D/security_audit.log" && ok "audit log has $ev" || bad "audit log has $ev"
done

# corruption -> recovery menu restores newest valid backup
cp -r "$D" "$WORK/d2"
python3 - "$WORK/d2/sms.db" <<'PY' 2>/dev/null || sed -i 's/Abel/Abex/' "$WORK/d2/sms.db"
import sys; p=sys.argv[1]; b=bytearray(open(p,'rb').read()); b[100]^=1; open(p,'wb').write(b)
PY
printf '%s\n' 1 /cancel | "$BIN" --data-dir "$WORK/d2" --ascii > "$WORK/s7.out" 2>&1
has "$WORK/s7.out" "checksum mismatch"                       "corrupt database detected"
has "$WORK/s7.out" "Restored from sms_"                      "newest valid backup restored"
ls "$WORK/d2" | grep -q "sms.db.corrupt-" && ok "damaged file kept aside" || bad "damaged file kept aside"

# single-instance lock
mkfifo "$WORK/fifo"; ( sleep 4 > "$WORK/fifo" & )
"$BIN" --data-dir "$D" < "$WORK/fifo" > /dev/null 2>&1 &
sleep 1; printf '/cancel\n' | "$BIN" --data-dir "$D" > /dev/null 2>&1
[ $? -eq 1 ] && ok "second instance refused (lock)" || bad "second instance refused (lock)"
wait

"$BIN" --bogus > /dev/null 2>&1; [ $? -eq 2 ] && ok "unknown option -> exit 2" || bad "unknown option -> exit 2"
"$BIN" --version | grep -q "sms 2" && ok "--version works" || bad "--version works"

echo; echo "$PASS passed, $FAIL failed"
[ "$FAIL" -eq 0 ]
