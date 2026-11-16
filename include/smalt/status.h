// Status values returned across module boundaries.
//
// CLAUDE.md, "Code style": no exceptions across the NickelHook boundary;
// functions return status values instead. Every module returns smalt::Status
// (or a struct that carries one) from anything that can fail.
//
// Toolchain limit: the Kobo Qt5 SDK ships GCC 4.9's C++ library, so these
// headers avoid <optional>, <variant> and <string_view>. See BUILDING.md.
#pragma once

#include <QString>

namespace smalt {

enum class StatusCode {
    Ok,
    NotFound, // A file, row or book that was expected is missing.
    Busy, // Try again later: database locked, book open, USB connected.
    InvalidArgument, // The caller passed something the callee refuses.
    IoError, // A file or database operation failed.
    Corrupt, // Integrity check failed (Crash safety, "Ledger protection").
    Unavailable, // A switch file, readiness state or capability says no.
    NotImplemented, // The scaffold stub; replaced by the owning task.
};

struct Status
{
    StatusCode code = StatusCode::Ok;
    QString message; // For the log only; never shown to the user as is.

    bool ok() const { return code == StatusCode::Ok; }

    static Status success() { return Status(); }

    static Status error(StatusCode code, const QString &message)
    {
        Status s;
        s.code = code;
        s.message = message;
        return s;
    }
};

} // namespace smalt
