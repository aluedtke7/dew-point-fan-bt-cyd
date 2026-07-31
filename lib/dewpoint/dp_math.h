// Dew-point math. Ported 1:1 from the Go `utility/math.go` (and the MicroPython
// `dewpoint/math.py`). Pure C++ with no Arduino dependencies so it is unit-
// testable on the host.
#pragma once

namespace dpf {

// Round `val` to `precision` decimal places, rounding half AWAY FROM ZERO.
// Mirrors Go's `math.Round` (and the MicroPython port), which differs from
// C++'s banker's-rounding-free but still-distinct default; keeping the explicit
// floor/ceil form guarantees identical results to the original controller.
double roundDouble(double val, int precision);

// Dew-point temperature (degC) from temperature (degC) and rel. humidity (%).
// Magnus formula with coefficients that switch at 0 degC, identical to the Go
// original. At <= 0% humidity the dew point is undefined; returns -infinity
// rather than computing log10(0).
double calcDewPoint(double temperature, double humidity);

}  // namespace dpf
