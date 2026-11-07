// Windows debugger / attach detection.
//
// Modest by itself - a determined reverser defeats every one of these - but
// cheap, and it raises the cost of the interactive step (setting breakpoints on
// the license path) that a first-time crack almost always starts from. Every
// positive routes through tamper::trip(); nothing fails on this file's lines.
//
// sweep() is safe to call repeatedly and from a timer: attaching a debugger
// mid-session is a common cracking move, so the checks are re-run, not just
// done once at startup.
#ifndef CUTPRO_GUARD_ANTI_DEBUG_H
#define CUTPRO_GUARD_ANTI_DEBUG_H

namespace guard {

// Run all available debugger checks once. Trips tamper state on any positive.
// Returns true if a debugger was detected (callers normally ignore the return
// and rely on the tamper flag).
bool sweep();

} // namespace guard

#endif // CUTPRO_GUARD_ANTI_DEBUG_H
