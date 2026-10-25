#pragma once
// Custom exception hierarchy. Every error that crosses a layer boundary is one of these,
// so the UI can decide how to present it without catching bare std::exception.
#include <stdexcept>
#include <string>

namespace sms {

class SmsException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Storage, serialization, integrity, locking and file-I/O failures.
class DatabaseException : public SmsException {
public:
    using SmsException::SmsException;
};

/// Authentication and authorization failures (bad credentials, lockout, RBAC denial).
class AuthException : public SmsException {
public:
    using SmsException::SmsException;
};

/// A value failed domain or format validation.
class ValidationException : public SmsException {
public:
    using SmsException::SmsException;
};

/// A requested entity does not exist.
class NotFoundException : public SmsException {
public:
    using SmsException::SmsException;
};

/// Standard input was closed (EOF). The application must stop prompting.
class InputClosedException : public SmsException {
public:
    InputClosedException() : SmsException("Input stream closed") {}
};

/// The user typed the cancel token while a form was open.
class OperationCancelled : public SmsException {
public:
    OperationCancelled() : SmsException("Operation cancelled") {}
};

}  // namespace sms
