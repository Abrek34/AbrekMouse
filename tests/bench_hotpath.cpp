#include "rawaccel.hpp"
#include "config.hpp"
#include "../daemon/motion_math.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <functional>
#include <fstream>
#include <cmath>
#include <map>
#include <filesystem>

using namespace rawaccel;

// Read CPU model from /proc/cpuinfo
static std::string get_cpu_model() {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string line;
    while (std::getline(cpuinfo, line)) {
        if (line.find("model name") != std::string::npos ||
            line.find("Hardware") != std::string::npos ||
            line.find("CPU implementer") != std::string::npos) {
            size_t colon = line.find(':');
            if (colon != std::string::npos) {
                std::string val = line.substr(colon + 1);
                size_t start = val.find_first_not_of(" \t");
                size_t end = val.find_last_not_of(" \t");
                if (start != std::string::npos) {
                    return val.substr(start, end - start + 1);
                }
            }
        }
    }
    return "unknown";
}

// Read CPU governor from sysfs
static std::string get_cpu_governor() {
    std::ifstream gov("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    std::string governor;
    if (std::getline(gov, governor)) {
        size_t end = governor.find_last_not_of(" \t\n\r");
        if (end != std::string::npos) {
            governor = governor.substr(0, end + 1);
        }
        return governor;
    }
    return "unreadable";
}

// Simple JSON parser for baseline file (no external deps)
static std::map<std::string, double> parse_baseline_json(const std::string& path) {
    std::map<std::string, double> baseline;
    std::ifstream f(path);
    if (!f) return baseline;
    std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    
    // Simple state machine to parse JSON and skip _meta object
    bool in_meta = false;
    int brace_depth = 0;
    size_t pos = 0;
    
    while (pos < content.size()) {
        char c = content[pos];
        
        // Track brace depth for object nesting
        if (c == '{') brace_depth++;
        else if (c == '}') brace_depth--;
        
        // Detect entering/leaving _meta object
        if (!in_meta && pos + 6 <= content.size() && content.compare(pos, 6, "\"_meta\"") == 0) {
            in_meta = true;
        }
        if (in_meta && brace_depth == 0) {
            in_meta = false;
        }
        
        // Only parse key-value pairs at top level (brace_depth == 1) and not in _meta
        if (!in_meta && brace_depth == 1 && c == '"') {
            size_t end_key = content.find('"', pos + 1);
            if (end_key == std::string::npos) break;
            std::string key = content.substr(pos + 1, end_key - pos - 1);
            size_t colon = content.find(':', end_key);
            if (colon == std::string::npos) break;
            size_t val_start = content.find_first_not_of(" \t\n", colon + 1);
            if (val_start == std::string::npos) break;
            size_t val_end = content.find_first_of(",\n}", val_start);
            if (val_end == std::string::npos) break;
            std::string val_str = content.substr(val_start, val_end - val_start);
            try {
                double val = std::stod(val_str);
                baseline[key] = val;
            } catch (...) {}
            pos = end_key + 1;
            continue;
        }
        
        pos++;
    }
    return baseline;
}

struct BenchResult {
    const char* name;
    double ns_per_event;
    uint64_t cycles;
    uint64_t instructions;
    uint64_t syscalls;
};

// Use clock_gettime for steady, high-resolution timing
static inline uint64_t now_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec);
}

// Calculate adaptive iterations for a config to achieve at least min_seconds per run
static int calc_iterations(double baseline_ns_per_event, double min_seconds = 0.1) {
    if (baseline_ns_per_event <= 0) return 100000;
    double iters = min_seconds / (baseline_ns_per_event * 1e-9);
    int result = static_cast<int>(std::ceil(iters));
    // Minimum 100k iterations
    if (result < 100000) result = 100000;
    // Maximum 10M iterations (safety cap)
    if (result > 10000000) result = 10000000;
    return result;
}

// Run a single configuration multiple times and return median ns/event
double run_config_median(const char* name, int runs, int iterations,
                         std::function<void(modifier&, speed_processor&, modifier_settings&, double, milliseconds, int, double&, double&, int&, int&, double&, double&)> setup_and_run,
                         bool positive_control, bool json_output) {
    std::vector<double> samples;
    samples.reserve(runs);

    for (int r = 0; r < runs; ++r) {
        double remainder_x = 0, remainder_y = 0;
        int out_x = 0, out_y = 0;
        double dx = 10.0, dy = 5.0;
        double dpi_factor = 1.0;
        milliseconds time_ms = 1;

        modifier mod;
        speed_processor sp;
        modifier_settings settings;

        setup_and_run(mod, sp, settings, dpi_factor, time_ms, iterations, remainder_x, remainder_y, out_x, out_y, dx, dy);

        const int pc_extra_work = positive_control ? 1000 : 0;

        auto start = now_ns();
        for (int i = 0; i < iterations; ++i) {
            apply_motion_math(mod, sp, settings, dpi_factor, time_ms, dx, dy, remainder_x, remainder_y, out_x, out_y);
            dx = -dx;
            dy = -dy;
            if (pc_extra_work > 0) {
                volatile int sink = 0;
                for (int j = 0; j < pc_extra_work; ++j) {
                    sink += j;
                }
                (void)sink;
            }
        }
        auto end = now_ns();

        // Consume outputs to prevent dead-code elimination
        volatile long long sink = static_cast<long long>(out_x) + static_cast<long long>(out_y) + static_cast<long long>(remainder_x * 1000) + static_cast<long long>(remainder_y * 1000);
        (void)sink;

        double total_ns = static_cast<double>(end - start);
        double ns_per_event = total_ns / iterations;
        
        // P110 HATA 2 FIX: Reject non-finite measurements (NaN/Inf)
        // NaN/Inf would corrupt median and cause silent false-OK in regression gate
        if (!std::isfinite(ns_per_event)) {
            std::cerr << "ERROR: Non-finite measurement for " << name << " (run " << r << "): " << ns_per_event << "\n";
            std::exit(1);
        }
        
        samples.push_back(ns_per_event);
    }

    std::sort(samples.begin(), samples.end());
    double median = samples[samples.size() / 2];
    if (!json_output) {
        std::cout << name << ": " << median << " ns/event (median of " << runs << " runs, " << iterations << " iterations each)\n";
    }
    return median;
}

int main(int argc, char** argv) {
    // C2 (L20-CRIT-2): baseline path resolution — no hard-coded absolute path.
    // Priority: explicit `--baseline PATH` → BENCH_BASELINE env var
    // (scripts/bench_hotpath.sh exports it) → paths relative to the executable
    // (build-manual/bench_hotpath → ../tests/perf_baseline.json) → paths
    // relative to CWD.  The first existing candidate wins;
    // weakly_canonical() resolves it to a usable absolute path.
    std::string baseline_override;
    auto resolve_baseline_path = [&]() -> std::string {
        namespace fs = std::filesystem;
        std::vector<std::string> candidates;
        if (!baseline_override.empty()) candidates.push_back(baseline_override);
        if (const char* env = std::getenv("BENCH_BASELINE"))
            if (*env) candidates.emplace_back(env);

        std::error_code ec;
        if (argv[0] && *argv[0]) {
            fs::path exe = fs::absolute(fs::path(argv[0]), ec);
            if (!ec) {
                fs::path dir = exe.parent_path();
                candidates.push_back((dir / "perf_baseline.json").string());
                candidates.push_back((dir / "../tests/perf_baseline.json").string());
                candidates.push_back((dir / "tests/perf_baseline.json").string());
            }
        }
        fs::path cwd = fs::current_path(ec);
        if (!ec) {
            candidates.push_back((cwd / "tests/perf_baseline.json").string());
            candidates.push_back((cwd / "perf_baseline.json").string());
        }
        for (const auto& c : candidates) {
            if (fs::exists(c)) {
                std::error_code ec2;
                fs::path canon = fs::weakly_canonical(c, ec2);
                return ec2 ? c : canon.string();
            }
        }
        return candidates.empty() ? std::string("tests/perf_baseline.json")
                                  : candidates.front();
    };

    // Parse arguments - handle flags first, then positional args
    int runs = 3;
    bool positive_control = false;
    bool json_output = false;
    double min_seconds = 0.1;  // Default 100ms per run

    if (std::getenv("BENCH_POSITIVE_CONTROL")) {
        positive_control = true;
    }

    // First pass: collect positional args (non-flags)
    std::vector<char*> positional_args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--positive-control") == 0) {
            positive_control = true;
        } else if (std::strcmp(argv[i], "--json") == 0) {
            json_output = true;
        } else if (std::strcmp(argv[i], "--baseline") == 0) {
            if (i + 1 < argc) {
                baseline_override = argv[i + 1];
                ++i;
            }
        } else if (std::strcmp(argv[i], "--min-seconds") == 0) {
            if (i + 1 < argc) {
                min_seconds = std::atof(argv[i + 1]);
                ++i;
            }
        } else {
            positional_args.push_back(argv[i]);
        }
    }

    // Load baseline for adaptive iterations
    std::string baseline_path = resolve_baseline_path();
    std::map<std::string, double> baseline = parse_baseline_json(baseline_path);

    // Config names in order
    const std::vector<std::string> config_names = {
        "noaccel",
        "power-whole",
        "classic",
        "power+rot45+snap15+clamp",
        "power-dual+4ema",
        "apply_motion_math(full)"
    };

    // Calculate iterations per config
    std::map<std::string, int> config_iterations;
    for (const auto& name : config_names) {
        auto it = baseline.find(name);
        if (it == baseline.end()) {
            std::cerr << "ERROR: Baseline missing required config: " << name << "\n";
            std::cerr << "Available keys: ";
            for (const auto& kv : baseline) {
                std::cerr << kv.first << " ";
            }
            std::cerr << "\n";
            return 1;
        }
        config_iterations[name] = calc_iterations(it->second, min_seconds);
    }

    std::vector<std::pair<std::string, double>> results;

    // Helper lambda to create a config and run it
    auto run_config = [&](const char* name, std::function<void(profile&)> setup_profile) {
        profile prof;
        setup_profile(prof);

        modifier_settings settings;
        settings.prof = prof;
        init_settings(settings);

        modifier mod;
        speed_processor sp;
        sp.init(prof.speed_processor_args);

        int iterations = config_iterations[name];

        double median = run_config_median(name, runs, iterations,
            [&](modifier& m, speed_processor& s, modifier_settings& st, double df, milliseconds tm, int it,
                double& rem_x, double& rem_y, int& o_x, int& o_y, double& d_x, double& d_y) {
                m = mod;
                s = sp;
                st = settings;
                df = 1.0;
                tm = 1;
                rem_x = 0; rem_y = 0;
                o_x = 0; o_y = 0;
                d_x = 10.0; d_y = 5.0;
            },
            positive_control, json_output);
        results.emplace_back(name, median);
    };

    // 1. noaccel / raw passthrough
    run_config("noaccel", [](profile& prof) {
        prof.raw_passthrough = true;
        prof.accel_x.mode = accel_mode::noaccel;
        prof.accel_y.mode = accel_mode::noaccel;
        prof.output_dpi = NORMALIZED_DPI;
        prof.speed_processor_args.whole = true;
    });

    // 2. power-whole (combined axis, no smoothing)
    run_config("power-whole", [](profile& prof) {
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::power;
        prof.accel_y.mode = accel_mode::power;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.scale = 2.2;
        prof.accel_y.scale = 2.2;
        prof.accel_x.exponent_power = 0.8;
        prof.accel_y.exponent_power = 0.8;
        prof.speed_processor_args.whole = true;
        prof.output_dpi = NORMALIZED_DPI;
    });

    // 3. classic (combined axis, no smoothing)
    run_config("classic", [](profile& prof) {
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::classic;
        prof.accel_y.mode = accel_mode::classic;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.acceleration = 0.005;
        prof.accel_y.acceleration = 0.005;
        prof.accel_x.exponent_classic = 2.0;
        prof.accel_y.exponent_classic = 2.0;
        prof.accel_x.limit = 1.8;
        prof.accel_y.limit = 1.8;
        prof.speed_processor_args.whole = true;
        prof.output_dpi = NORMALIZED_DPI;
    });

    // 4. power + rotation 45° + snap 15° + speed clamp
    run_config("power+rot45+snap15+clamp", [](profile& prof) {
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::power;
        prof.accel_y.mode = accel_mode::power;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.scale = 2.2;
        prof.accel_y.scale = 2.2;
        prof.accel_x.exponent_power = 0.8;
        prof.accel_y.exponent_power = 0.8;
        prof.speed_processor_args.whole = true;
        prof.degrees_rotation = 45.0;
        prof.degrees_snap = 15.0;
        prof.speed_min = 10.0;
        prof.speed_max = 4000.0;
        prof.output_dpi = NORMALIZED_DPI;
    });

    // 5. power dual-axis + 4 EMA smoothers (input, scale, output x2)
    run_config("power-dual+4ema", [](profile& prof) {
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::power;
        prof.accel_y.mode = accel_mode::power;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.scale = 2.2;
        prof.accel_y.scale = 2.2;
        prof.accel_x.exponent_power = 0.8;
        prof.accel_y.exponent_power = 0.8;
        prof.speed_processor_args.whole = false;
        prof.speed_processor_args.input_speed_smooth_halflife = 1.0;
        prof.speed_processor_args.scale_smooth_halflife = 1.0;
        prof.speed_processor_args.output_speed_smooth_halflife = 1.0;
        prof.output_dpi = NORMALIZED_DPI;
    });

    // 6. full apply_motion_math with subpixel (classic, whole, no smoothing)
    run_config("apply_motion_math(full)", [](profile& prof) {
        prof.raw_passthrough = false;
        prof.accel_x.mode = accel_mode::classic;
        prof.accel_y.mode = accel_mode::classic;
        prof.accel_x.gain = true;
        prof.accel_y.gain = true;
        prof.accel_x.acceleration = 0.005;
        prof.accel_y.acceleration = 0.005;
        prof.accel_x.exponent_classic = 2.0;
        prof.accel_y.exponent_classic = 2.0;
        prof.accel_x.limit = 1.8;
        prof.accel_y.limit = 1.8;
        prof.speed_processor_args.whole = true;
        prof.output_dpi = NORMALIZED_DPI;
    });

    // Output
    if (json_output) {
        std::cout << "{\n";
        for (size_t i = 0; i < results.size(); ++i) {
            std::cout << "  \"" << results[i].first << "\": " << std::fixed << std::setprecision(4) << results[i].second << ",\n";
        }
        std::cout << "  \"_meta\": {\n";
        std::cout << "    \"unit\": \"ns/event\",\n";
        std::cout << "    \"iterations_per_config\": {\n";
        for (size_t i = 0; i < config_names.size(); ++i) {
            std::cout << "      \"" << config_names[i] << "\": " << config_iterations[config_names[i]];
            if (i + 1 < config_names.size()) std::cout << ",";
            std::cout << "\n";
        }
        std::cout << "    },\n";
        std::cout << "    \"runs_used\": " << runs << ",\n";
        std::cout << "    \"method\": \"median\",\n";
        std::cout << "    \"min_seconds_per_run\": " << min_seconds << ",\n";
        std::cout << "    \"positive_control\": " << (positive_control ? "true" : "false") << ",\n";
        std::cout << "    \"cpu_model\": \"" << get_cpu_model() << "\",\n";
        std::cout << "    \"cpu_governor\": \"" << get_cpu_governor() << "\"\n";
        std::cout << "  }\n";
        std::cout << "}\n";
    } else {
        std::cout << "\n=== SUMMARY ===\n";
        for (const auto& r : results) {
            std::cout << r.first << ": " << r.second << " ns/event (iterations=" << config_iterations[r.first] << ")\n";
        }
    }

    return 0;
}