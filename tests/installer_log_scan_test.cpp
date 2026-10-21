// Focused regression test for the installer's failure-marker scan.
//
// The success line printed by setup.sh's verify_install() is
// "Her şey yerinde — kurulum EKSİKSİZ." — it contains the substring "EKSİK".
// An earlier scan matched that substring and would have marked every successful
// installation as failed.  The installer now keys only on err()'s "✗" marker.

#include "log_scan.hpp"

#include <cstdio>
#include <string>

using rawaccel_installer::log_has_error;
using rawaccel_installer::log_has_package_lock;

int main() {
    int failures = 0;

    if (log_has_error("Her şey yerinde — kurulum EKSİKSİZ.\n"))
        ++failures; // success text must never look like an error

    if (!log_has_error("  ✗ Binary EKSİK: rawaccel-gui\n"))
        ++failures;

    const std::string split_first = "\xE2\x9C"; // first two UTF-8 bytes of ✗
    if (log_has_error(split_first))
        ++failures;
    const std::string split_full = split_first + "\x97 daha sonra";
    if (!log_has_error(split_full))
        ++failures;

    const std::string with_prefix = "önemsiz\n  ✗ Servis ÇALIŞMIYOR\n";
    if (!log_has_error(with_prefix, 0))
        ++failures;
    if (log_has_error(with_prefix, with_prefix.size()))
        ++failures;

    if (!log_has_package_lock("hata: işlem (veritabanı kilitlenemiyor) başlatılamadı\n"))
        ++failures;
    if (!log_has_package_lock("error: failed to init transaction (unable to lock database)\n"))
        ++failures;
    if (log_has_package_lock("Her şey yerinde — kurulum EKSİKSİZ.\n"))
        ++failures;

    if (failures) {
        std::fprintf(stderr, "installer log scan: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("installer log scan: OK");
    return 0;
}
