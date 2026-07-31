#include "dp_math.h"

#include <cmath>
#include <limits>

namespace dpf {

double roundDouble(double val, int precision) {
  const double ratio = std::pow(10.0, precision);
  const double x = val * ratio;
  // Half away from zero (matches Go's math.Round / the MicroPython round_double).
  const double rounded = (x >= 0.0) ? std::floor(x + 0.5) : std::ceil(x - 0.5);
  return rounded / ratio;
}

double calcDewPoint(double temperature, double humidity) {
  if (humidity <= 0.0) {
    return -std::numeric_limits<double>::infinity();
  }

  double a, b;
  if (temperature >= 0.0) {
    a = 7.5;
    b = 237.3;
  } else {
    a = 7.6;
    b = 240.7;
  }

  // saturation vapor pressure in hPa
  const double sdd = 6.1078 * std::pow(10.0, (a * temperature) / (b + temperature));
  // vapor pressure in hPa
  const double dd = sdd * (humidity / 100.0);
  // v parameter
  const double v = std::log10(dd / 6.1078);
  // dew point temperature (degC), rounded to 1 decimal
  return roundDouble((b * v) / (a - v), 1);
}

}  // namespace dpf
