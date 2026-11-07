#pragma once

#include <optional>
#include <string_view>

// Enumerations for the Cut Pro domain model (port of core/enums.py).
// The string values match the Python str-enums so the JSON wire format is
// identical, e.g. TrackKind::Video <-> "video".
namespace core {

enum class TrackKind { Video, Audio };
enum class ClipKind { Video, Audio, Image, Subtitle };
enum class MediaKind { Video, Audio, Image, Unknown };

inline std::string_view to_string(TrackKind k) {
    switch (k) {
        case TrackKind::Video: return "video";
        case TrackKind::Audio: return "audio";
    }
    return {};
}

inline std::string_view to_string(ClipKind k) {
    switch (k) {
        case ClipKind::Video:    return "video";
        case ClipKind::Audio:    return "audio";
        case ClipKind::Image:    return "image";
        case ClipKind::Subtitle: return "subtitle";
    }
    return {};
}

inline std::string_view to_string(MediaKind k) {
    switch (k) {
        case MediaKind::Video:   return "video";
        case MediaKind::Audio:   return "audio";
        case MediaKind::Image:   return "image";
        case MediaKind::Unknown: return "unknown";
    }
    return {};
}

inline std::optional<TrackKind> track_kind_from_string(std::string_view s) {
    if (s == "video") return TrackKind::Video;
    if (s == "audio") return TrackKind::Audio;
    return std::nullopt;
}

inline std::optional<ClipKind> clip_kind_from_string(std::string_view s) {
    if (s == "video")    return ClipKind::Video;
    if (s == "audio")    return ClipKind::Audio;
    if (s == "image")    return ClipKind::Image;
    if (s == "subtitle") return ClipKind::Subtitle;
    return std::nullopt;
}

inline std::optional<MediaKind> media_kind_from_string(std::string_view s) {
    if (s == "video")   return MediaKind::Video;
    if (s == "audio")   return MediaKind::Audio;
    if (s == "image")   return MediaKind::Image;
    if (s == "unknown") return MediaKind::Unknown;
    return std::nullopt;
}

}  // namespace core
