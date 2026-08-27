#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// A tiny synchronous, single-threaded multicast callback list — the C++ port
// of core/events.py Signal. Framework-independent (no Qt); the Qt bridge is
// responsible for marshalling onto the GUI thread.
//
// Difference from the Python version: connect() returns an opaque Id used to
// disconnect(), because std::function is not identity-comparable, so Python's
// "disconnect by the same callable" cannot be expressed directly.
namespace core {

template <class... Args>
class Signal {
public:
    using Slot = std::function<void(Args...)>;
    using Id = std::uint64_t;

    explicit Signal(std::string name = "") : name_(std::move(name)) {}

    // Register a slot. Returns an Id for later disconnect().
    Id connect(Slot slot) {
        const Id id = ++last_id_;
        slots_.push_back({id, std::move(slot)});
        return id;
    }

    // Unregister a previously connected slot (no-op if not present).
    void disconnect(Id id) {
        for (auto it = slots_.begin(); it != slots_.end(); ++it) {
            if (it->id == id) {
                slots_.erase(it);
                return;
            }
        }
    }

    // Call every connected slot. Iterates a copy so slots may connect or
    // disconnect during emission (matches the Python semantics).
    void emit(Args... args) const {
        const auto snapshot = slots_;
        for (const auto& entry : snapshot)
            entry.slot(args...);
    }

    void clear() { slots_.clear(); }
    std::size_t size() const { return slots_.size(); }
    const std::string& name() const { return name_; }

private:
    struct Entry {
        Id id;
        Slot slot;
    };
    std::string name_;
    std::vector<Entry> slots_;
    Id last_id_ = 0;
};

}  // namespace core
