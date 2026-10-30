#include "xmltv/timezone.hpp"

namespace xmltv {
namespace {

namespace c = std::chrono;

// EU rule (Directive 2000/84/EC): switches at 01:00 UTC on the last Sunday.
Time eu_switch(c::year year, c::month month) {
    return c::sys_days{year / month / c::Sunday[c::last]} + c::hours{1};
}

}  // namespace

TimeZone utc_zone() {
    return TimeZone{"UTC", "UTC", c::minutes{0}, false, "UTC", "UTC"};
}

TimeZone stockholm_zone() {
    return TimeZone{"Europe/Stockholm", "Swedish time", c::hours{1}, true, "CET", "CEST"};
}

std::optional<TimeZone> find_zone(std::string_view name) {
    if (name == "UTC" || name == "utc") return utc_zone();
    if (name == "Europe/Stockholm" || name == "sweden" || name == "Sweden") {
        return stockholm_zone();
    }
    return std::nullopt;
}

bool in_summer_time(const TimeZone& zone, Time t) {
    if (!zone.eu_summer_time) return false;
    const c::year year = c::year_month_day{c::floor<c::days>(t)}.year();
    return t >= eu_switch(year, c::March) && t < eu_switch(year, c::October);
}

c::minutes utc_offset(const TimeZone& zone, Time t) {
    return zone.standard_offset + (in_summer_time(zone, t) ? c::hours{1} : c::hours{0});
}

const std::string& abbreviation(const TimeZone& zone, Time t) {
    return in_summer_time(zone, t) ? zone.summer_abbrev : zone.standard_abbrev;
}

}  // namespace xmltv
