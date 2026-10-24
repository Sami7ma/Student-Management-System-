#pragma once
// Text persistence format "SMSDB v2": magic header, one escaped tab-separated record per line,
// and a trailer carrying the record count and a SHA-256 of everything before it.
//
//   SMSDB<TAB>2
//   U<TAB>username<TAB>role<TAB>hash<TAB>linkedStudentId<TAB>active<TAB>mustChange<TAB>createdAt
//   S<TAB>id<TAB>name<TAB>email<TAB>phone<TAB>department<TAB>dob<TAB>enrolledOn<TAB>active<TAB>deletedAt
//   C<TAB>code<TAB>title<TAB>credits<TAB>ects<TAB>department<TAB>active
//   E<TAB>studentId<TAB>courseCode<TAB>term<TAB>mark<TAB>recordedAt
//   END<TAB>recordCount<TAB>sha256hex
#include <string>
#include <string_view>

#include "sms/models/Database.hpp"

namespace sms::storage {

constexpr std::string_view kMagic = "SMSDB";
constexpr int kSchemaVersion = 2;

std::string serialize(const Database& db);
/// Parses and fully validates (checksum, schema, domain rules, referential integrity).
/// Throws DatabaseException with a line number on any problem.
Database deserialize(std::string_view content);

}  // namespace sms::storage
