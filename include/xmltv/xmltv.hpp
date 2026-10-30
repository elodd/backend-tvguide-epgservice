#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace xmltv {

using Time = std::chrono::sys_seconds;  // always UTC

struct Channel {
    std::string id;
    std::vector<std::string> display_names;
};

struct Programme {
    std::string channel_id;
    Time start;
    Time stop;
    std::string title;
    std::string description;
    std::vector<std::string> categories;
};

struct Guide {
    std::vector<Channel> channels;
    std::vector<Programme> programmes;  // sorted by (channel_id, start)
};

enum class ErrorKind { MalformedXml, MissingAttribute, BadTimestamp, BadSource, Download, Decompress };

struct Error {
    ErrorKind kind;
    std::string message;
};

template <class T>
using Result = std::variant<T, Error>;

// Parses an XMLTV document. Malformed programmes are reported as errors;
// zero-length programmes are skipped; programmes referencing unknown channels are kept.
Result<Guide> parse(std::string_view xml);

// Parses "YYYYMMDDHHMM[SS] [+-HHMM]". Without an offset the time is taken as UTC.
Result<Time> parse_timestamp(std::string_view text);

}  // namespace xmltv
