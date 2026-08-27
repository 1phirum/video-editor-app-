#pragma once

// Framework-independent domain core version (ported from core/__init__.py
// __version__). Kept in one place so the Qt bridge and any future serializer
// can stamp it without pulling in Qt.
namespace core {
inline constexpr const char* kVersion = "0.1.2";
}
