#pragma once

#include "xmltv/timezone.hpp"
#include "xmltv/xmltv.hpp"

#include <string>

namespace xmltv {

// A self-contained, printable HTML page with one section per day in `zone` (UTC by
// default) and, inside it, one colour-coded box per channel with that day's programmes.
// Days are broadcast days: a channel's day runs from its morning time (the time of day of
// its first programme, at the latest 06:00) to the next morning, so programmes after midnight stay in the
// previous day's box. On screen, an inline script shows one day at a time via tabs,
// filters channels by name and highlights what is on now. Channels listed in the guide come first
// in file order, followed by any channels that appear only in programmes; channels
// without programmes are named once at the end.
std::string render_schedule_html(const Guide& guide, const TimeZone& zone = utc_zone());

}  // namespace xmltv
