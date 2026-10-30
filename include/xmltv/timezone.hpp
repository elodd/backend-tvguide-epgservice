#pragma once

#include "xmltv/xmltv.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace xmltv {

// A display time zone with a fixed standard offset and, optionally, EU summer
// time (+1h from 01:00 UTC on the last Sunday of March to 01:00 UTC on the last
// Sunday of October). Covers UTC and Sweden without needing a tz database,
// which Apple's libc++ does not ship.
struct TimeZone {
    std::string name;          // e.g. "Europe/Stockholm"
    std::string label;         // shown on the page, e.g. "Swedish time"
    std::chrono::minutes standard_offset{0};
    bool eu_summer_time = false;
    std::string standard_abbrev;  // e.g. "CET"
    std::string summer_abbrev;    // e.g. "CEST"
};

TimeZone utc_zone();
TimeZone stockholm_zone();

// Looks up a zone by IANA name ("UTC", "Europe/Stockholm") or alias ("sweden").
std::optional<TimeZone> find_zone(std::string_view name);

bool in_summer_time(const TimeZone& zone, Time t);
std::chrono::minutes utc_offset(const TimeZone& zone, Time t);
const std::string& abbreviation(const TimeZone& zone, Time t);

}  // namespace xmltv
