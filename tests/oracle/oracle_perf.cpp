// P138 — oracle_perf cross-check micro-benchmark.
//
// Compares the *apply()* hot path of the LOCAL port (include/accel-union.hpp)
// against the vendored OFFICIAL reference (tests/oracle/ref/accel-union.hpp)
// over the SAME shared oracle parameter grid, using wall-clock nanoseconds and
// x86-64 cycle counts per apply() call.
//
// Build via run_oracle_perf.sh (or manually):
//   LOCAL side:  g++ -std=c++20 -O3 -march=native -DORACLE_PERF_LOCAL    \
//                -I<repo> oracle_perf.cpp -o perf_local
//   REF side:    g++ -std=c++20 -O3 -march=native -DORACLE_PERF_REF -fpermissive \
//                -Wno-changes-meaning -I<repo>/tests/oracle/ref           \
//                -include <repo>/tests/oracle/ref/refcompat.hpp           \
//                oracle_perf.cpp -o perf_ref
//
// Output (per side): one line per case-mean  rpt=<reps> mean_ns=... ns/apply.
// The final "TOTAL mean_ns" line is the grid-wide average per apply() call.

#include "oracle_cases.hpp"

#if defined(ORACLE_PERF_LOCAL)
#include "include/accel-union.hpp"          // local port, <variant>-based
#elif defined(ORACLE_PERF_REF)
#include "accel-union.hpp"                  // vendored official reference
#endif

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <variant>
#include <vector>

using namespace rawaccel;

namespace {

accel_args build_args(const oracle::Case& k) {
    accel_args a;
    a.mode = (k.mode == "classic") ? accel_mode::classic
           : (k.mode == "jump") ? accel_mode::jump
           : (k.mode == "lookup") ? accel_mode::lookup
           : (k.mode == "natural") ? accel_mode::natural
           : (k.mode == "synchronous") ? accel_mode::synchronous
           : (k.mode == "power") ? accel_mode::power
           : accel_mode::noaccel;
    a.gain = k.gain;
    a.acceleration     = k.acceleration;
    a.scale            = k.scale;
    a.decay_rate       = k.decay_rate;
    a.gamma            = k.gamma;
    a.motivity         = k.motivity;
    a.exponent_classic = k.exponent_classic;
    a.exponent_power   = k.exponent_power;
    a.limit            = k.limit;
    a.sync_speed       = k.sync_speed;
    a.smooth           = k.smooth;
    a.input_offset     = k.input_offset;
    a.output_offset    = k.output_offset;
    a.cap              = { k.cap_x, k.cap_y };
#if defined(ORACLE_PERF_REF)
    a.cap_mode         = static_cast<cap_mode>(k.cap_mode);
#else
    a.cap_mode_val     = static_cast<cap_mode>(k.cap_mode);
#endif
    a.length           = static_cast<int>(k.lut.size());
    a.data[0]          = 0;
    for (size_t i = 0; i < k.lut.size() && i < LUT_RAW_DATA_CAPACITY; ++i)
        a.data[i] = k.lut[i];
    return a;
}

inline uint64_t rdtsc() {
    unsigned lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t(hi) << 32) | lo;
}

struct timed_run { double ns; uint64_t cyc; };

#if defined(ORACLE_PERF_REF)
// Fair hot-path measurement: RawAccel constructs the accel object once per
// config change and applies it per event. The vendored reference builds each
// accel from accel_args, so we hold one constructed instance per case in a
// variant (the raw union cannot be default-constructed) and time only the
// apply() call.
using ref_union = std::variant<
    accel_noaccel,
    lookup,
    classic<GAIN>, classic<LEGACY>,
    jump<GAIN>, jump<LEGACY>,
    natural<GAIN>, natural<LEGACY>,
    power<GAIN>, power<LEGACY>,
    activation_framework<GAIN>, activation_framework<LEGACY>>;

template <template <bool> class Accel>
ref_union make_ref(const accel_args& a) {
    if (a.gain) return Accel<GAIN>(a);
    return Accel<LEGACY>(a);
}
ref_union build_ref(const accel_args& a) {
    switch (a.mode) {
    case accel_mode::classic:     return make_ref<classic>(a);
    case accel_mode::jump:        return make_ref<jump>(a);
    case accel_mode::natural:     return make_ref<natural>(a);
    case accel_mode::synchronous: return make_ref<activation_framework>(a);
    case accel_mode::power:       return make_ref<power>(a);
    case accel_mode::lookup:      return lookup(a);
    default:                      return accel_noaccel(a);
    }
}
double apply_ref(const ref_union& u, double spd, const accel_args& a) {
    return std::visit([&](const auto& impl) { return impl(spd, a); }, u);
}
#else
double apply_one(accel_union& au, double spd, const accel_args& a) {
    return au.apply(spd, a);
}
#endif

timed_run bench(const oracle::Case& k, long reps) {
    accel_args a = build_args(k);
    std::vector<double> speeds = k.speeds;

#if defined(ORACLE_PERF_REF)
    // Vendored reference built once (mirrors RawAccel: config change builds
    // the accel object, every mouse event just applies it).
    ref_union ru = build_ref(a);
#else
    accel_union au;
    au.init(a);
#endif

    // warm-up + establish expected value so the compiler cannot sink the calls.
    volatile double sink = 0.0;
    double expect = 0;
    for (double s : speeds) {
#if defined(ORACLE_PERF_REF)
        expect += apply_ref(ru, s, a);
#else
        expect += apply_one(au, s, a);
#endif
    }

    auto t0 = std::chrono::steady_clock::now();
    uint64_t c0 = rdtsc();
    for (long r = 0; r < reps; ++r) {
        for (double s : speeds) {
            double g;
#if defined(ORACLE_PERF_REF)
            g = apply_ref(ru, s, a);
#else
            g = apply_one(au, s, a);
#endif
            sink += g * (g - g * 0.5); // non-trivial sink, keeps the call alive
        }
    }
    uint64_t c1 = rdtsc();
    auto t1 = std::chrono::steady_clock::now();

    (void)expect;
    long calls = reps * static_cast<long>(speeds.size());
    double ns   = std::chrono::duration<double, std::nano>(t1 - t0).count() / calls;
    uint64_t cyc = (c1 > c0) ? (c1 - c0) / static_cast<uint64_t>(calls) : 0;
    return { ns, cyc };
}

} // namespace

int main(int argc, char** argv) {
    long reps = (argc > 1) ? std::atol(argv[1]) : 5;
    const char* side = "local";
#if defined(ORACLE_PERF_REF)
    side = "ref";
#endif
    std::vector<oracle::Case> all = oracle::cases();
    std::printf("# oracle_perf side=%s reps=%ld cases=%zu  (ns and cycles per apply call)\n",
                side, reps, all.size());
    double total_ns = 0; uint64_t total_cyc = 0; long total_calls = 0;
    for (const auto& k : all) {
        timed_run t = bench(k, reps);
        long calls = reps * static_cast<long>(k.speeds.size());
        std::printf("%-18s reps=%-3ld calls=%-5ld mean_ns=%.2f mean_cyc=%llu\n",
                    k.name.c_str(), reps, calls, t.ns,
                    static_cast<unsigned long long>(t.cyc));
        total_ns += t.ns * calls; total_cyc += t.cyc * calls; total_calls += calls;
    }
    std::printf("TOTAL mean_ns=%.2f mean_cyc=%llu calls=%ld\n",
                total_ns / total_calls,
                static_cast<unsigned long long>(double(total_cyc) / total_calls),
                total_calls);
    return 0;
}