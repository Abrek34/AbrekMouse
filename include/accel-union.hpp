#pragma once
#include "accel-classic.hpp"
#include "accel-jump.hpp"
#include "accel-lookup.hpp"
#include "accel-synchronous.hpp"
#include "accel-natural.hpp"
#include "accel-noaccel.hpp"
#include "accel-power.hpp"

#include <variant>

namespace rawaccel {

using accel_variant = std::variant<
    accel_noaccel,
    classic,
    power,
    natural,
    jump,
    synchronous,
    lookup
>;

/// Holds one concrete accelerator chosen at runtime.
struct accel_union {
    accel_variant impl;

    accel_union() : impl(accel_noaccel{}) {}

    void init(const accel_args& args) {
        switch (args.mode) {
        case accel_mode::classic:     impl = classic(args);     break;
        case accel_mode::power:       impl = power(args);       break;
        case accel_mode::natural:     impl = natural(args);     break;
        case accel_mode::jump:        impl = jump(args);        break;
        case accel_mode::synchronous: impl = synchronous(args); break;
        case accel_mode::lookup:      impl = lookup(args);      break;
        case accel_mode::noaccel:
        default:                      impl = accel_noaccel{};   break;
        }
    }

    /// Returns the gain multiplier for the given input speed.
    /// PERF (P0): explicit switch dispatch over the variant index instead of
    /// std::visit.  The libstdc++ __do_visit machinery is huge; once
    /// modifier::modify grew past GCC's inline budget it was cloned out-of-line
    /// (an .isra.0 call on every event, roughly doubling non-smoothing cost).
    /// A plain switch + std::get compiles to a branch with no template
    /// machinery, and because the active index is loop-invariant per device the
    /// inlined code prunes every other branch down to one direct call.
    inline
    double apply(double speed, const accel_args& args) const {
        switch (impl.index()) {
        case 0:  return std::get<0>(impl)(speed, args);
        case 1:  return std::get<1>(impl)(speed, args);
        case 2:  return std::get<2>(impl)(speed, args);
        case 3:  return std::get<3>(impl)(speed, args);
        case 4:  return std::get<4>(impl)(speed, args);
        case 5:  return std::get<5>(impl)(speed, args);
        default: return std::get<6>(impl)(speed, args);
        }
    }
};

} // namespace rawaccel
