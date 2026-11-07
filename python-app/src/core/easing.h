#pragma once

#include <algorithm>
#include <cmath>

namespace core {

// Temporal ease between two keyframes, as a CSS-style cubic Bezier timing curve
// with control points (easeOut, 0) and (1 - easeIn, 1). `t` and the result are
// both 0..1.
//
// Those two endpoints are what make the mapping honest: with both influences at
// zero the control points are (0,0) and (1,1), which is an exact straight line,
// so a curve nobody has eased interpolates bit-for-bit the way it did before
// easing existed. Equal influences of 0.42 give the familiar ease-in-out.
//
// This lives in core, header-only, because two modules have to agree on it and
// they cannot call each other: cutpro_timeline evaluates it for the program
// monitor, and cutpro_effects subdivides eased segments with it when building
// the export filtergraph - but cutpro_timeline already links cutpro_effects, so
// the dependency cannot run the other way. A second copy of the curve is the one
// thing that would make the monitor and the rendered file disagree.
inline double cubicBezierEase(double t, double easeOut, double easeIn) {
  const double x1 = std::clamp(easeOut, 0.0, 1.0);
  const double x2 = 1.0 - std::clamp(easeIn, 0.0, 1.0);
  if (x1 == 0.0 && x2 == 1.0)
    return t; // the linear case, kept exact rather than solved
  // x(u) and y(u) for control points (x1,0) and (x2,1); y1 = 0, y2 = 1.
  const auto bezierX = [x1, x2](double u) {
    const double v = 1.0 - u;
    return 3.0 * v * v * u * x1 + 3.0 * v * u * u * x2 + u * u * u;
  };
  const auto bezierDx = [x1, x2](double u) {
    const double v = 1.0 - u;
    return 3.0 * v * v * x1 + 6.0 * v * u * (x2 - x1) +
           3.0 * u * u * (1.0 - x2);
  };
  // Invert x(u) = t. Newton converges in a couple of steps across most of the
  // curve; the bisection fallback covers the flat spots where the derivative is
  // too small for Newton to be trusted.
  double u = t;
  for (int i = 0; i < 8; ++i) {
    const double dx = bezierX(u) - t;
    if (std::abs(dx) < 1e-6)
      break;
    const double d = bezierDx(u);
    if (std::abs(d) < 1e-6)
      break;
    u -= dx / d;
  }
  if (u < 0.0 || u > 1.0 || std::abs(bezierX(u) - t) > 1e-5) {
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 32; ++i) {
      u = 0.5 * (lo + hi);
      if (bezierX(u) < t)
        lo = u;
      else
        hi = u;
    }
  }
  const double v = 1.0 - u;
  return 3.0 * v * u * u + u * u * u;
}

} // namespace core
