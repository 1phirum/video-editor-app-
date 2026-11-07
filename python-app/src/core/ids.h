#pragma once

#include <string>

// Stable identifier helper for domain objects (port of core/ids.py).
// Every domain entity carries a short, opaque, collision-resistant id.
namespace core {

// Return a short unique id like "clip_9f3a1c2b4d5e": the human-readable
// prefix, an underscore, then 12 lowercase hex characters.
std::string new_id(const std::string& prefix = "id");

}
