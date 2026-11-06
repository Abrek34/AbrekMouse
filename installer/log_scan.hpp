#pragma once

#include <cstddef>
#include <string>

namespace rawaccel_installer {

// setup.sh prefixes every error() line with U+2717 BALLOT X ("✗").  This is
// the only failure marker the installer trusts: searching for the bare word
// "EKSİK" would also match the success line "kurulum EKSİKSİZ" and mark every
// good install as failed.  Keep this helper small and testable.
inline bool log_has_error(const std::string& log, std::size_t from = 0) {
    return log.find("✗", from) != std::string::npos;
}

// setup.sh delegates dependency installation to pacman.  A stale
// /var/lib/pacman/db.lck is common after an interrupted system upgrade; the
// installer can offer a GUI-side cleanup instead of sending the user to a
// terminal.  Recognize both the Turkish and English pacman diagnostics.
inline bool log_has_package_lock(const std::string& log) {
    static const char* const markers[] = {
        "db.lck",
        "veritabanı kilitlenemiyor",
        "veritabanı kilitli",
        "could not lock database",
        "unable to lock database",
        "failed to init transaction",
        "database is locked",
    };
    for (const char* marker : markers) {
        if (log.find(marker) != std::string::npos) return true;
    }
    return false;
}

} // namespace rawaccel_installer
