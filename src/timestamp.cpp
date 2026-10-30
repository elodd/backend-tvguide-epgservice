#include "xmltv/xmltv.hpp"

#include <charconv>
#include <string>

namespace xmltv {
namespace {

namespace c = std::chrono;

bool to_int(std::string_view s, int& out) {
    if (s.empty()) return false;
    const auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && ptr == s.data() + s.size();
}

Error bad_timestamp(std::string_view text) {
    return Error{ErrorKind::BadTimestamp,
                 "invalid XMLTV timestamp: '" + std::string(text) + "'"};
}

}  // namespace

Result<Time> parse_timestamp(std::string_view text) {
    if (text.size() < 14) return bad_timestamp(text);

    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!to_int(text.substr(0, 4), year) || !to_int(text.substr(4, 2), month) ||
        !to_int(text.substr(6, 2), day) || !to_int(text.substr(8, 2), hour) ||
        !to_int(text.substr(10, 2), minute) || !to_int(text.substr(12, 2), second)) {
        return bad_timestamp(text);
    }

    const c::year_month_day ymd{c::year{year},
                                c::month{static_cast<unsigned>(month)},
                                c::day{static_cast<unsigned>(day)}};
    if (!ymd.ok() || hour > 23 || minute > 59 || second > 59) {
        return bad_timestamp(text);
    }

    std::string_view rest = text.substr(14);
    while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);

    int offset_minutes = 0;
    if (!rest.empty()) {
        if (rest.size() != 5 || (rest[0] != '+' && rest[0] != '-')) {
            return bad_timestamp(text);
        }
        int hh = 0, mm = 0;
        if (!to_int(rest.substr(1, 2), hh) || !to_int(rest.substr(3, 2), mm) ||
            hh > 23 || mm > 59) {
            return bad_timestamp(text);
        }
        offset_minutes = (hh * 60 + mm) * (rest[0] == '-' ? -1 : 1);
    }

    const Time midnight = c::sys_days{ymd};
    const Time local = midnight + c::hours{hour} + c::minutes{minute} + c::seconds{second};
    return local - c::minutes{offset_minutes};
}

}  // namespace xmltv
