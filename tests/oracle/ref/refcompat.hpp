#pragma once
// Minimal g++ (Linux) compatibility shims for the vendored RawAccel reference
// headers (originally written for MSVC). Keeps the reference math verbatim.
#include <cmath>
#define __forceinline inline
#define _copysign std::copysign
