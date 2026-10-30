#include "xmltv/html.hpp"
#include "xmltv/timezone.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <string_view>
#include <vector>

namespace xmltv {
namespace {

// Channel boxes and category chips are tinted by a hue (--h) derived from their name, so
// each keeps its colour across days. Lightness switches with the colour scheme.
constexpr std::string_view kStyle = R"CSS(
:root {
  --fg:#1d1d1f; --muted:#6e6e73; --border:#e2e2e7; --bg:#f2f2f7; --card:#ffffff;
  --accent:#0a66d8; --on-accent:#ffffff; --now:#fff3cd;
  --band-l:48%; --tint-l:94%; --ink-l:28%;
}
@media (prefers-color-scheme: dark) {
  :root {
    --fg:#f5f5f7; --muted:#a1a1a6; --border:#38383a; --bg:#000000; --card:#1c1c1e;
    --accent:#4c9dff; --on-accent:#000000; --now:#3d3414;
    --band-l:60%; --tint-l:20%; --ink-l:84%;
  }
}
* { box-sizing:border-box; }
[hidden] { display:none !important; }
body { margin:0; font:15px/1.45 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; color:var(--fg); background:var(--bg); }
.top { position:sticky; top:0; z-index:1; padding:14px 20px 10px; background:color-mix(in srgb, var(--bg) 88%, transparent); backdrop-filter:blur(12px); -webkit-backdrop-filter:blur(12px); border-bottom:1px solid var(--border); }
.bar { display:flex; gap:12px; align-items:baseline; flex-wrap:wrap; }
h1 { font-size:1.6em; margin:0; }
.lead { color:var(--muted); margin:0; flex:1; }
.bar button { font:inherit; padding:5px 14px; border:1px solid var(--border); border-radius:999px; background:var(--card); color:var(--fg); cursor:pointer; }
.controls { display:flex; gap:10px; align-items:center; flex-wrap:wrap; margin-top:10px; }
.tabs { display:flex; gap:6px; flex-wrap:wrap; }
.tab { padding:5px 12px; border:1px solid var(--border); border-radius:999px; background:var(--card); color:var(--fg); text-decoration:none; font-size:14px; white-space:nowrap; }
.tab[aria-current="page"] { background:var(--accent); border-color:var(--accent); color:var(--on-accent); font-weight:600; }
#filter { font:inherit; font-size:14px; padding:5px 12px; border:1px solid var(--border); border-radius:999px; background:var(--card); color:var(--fg); min-width:200px; flex:1; max-width:320px; }
main { padding:16px 20px 24px; }
.day + .day { margin-top:32px; }
.day > h2 { font-size:1.3em; margin:0 0 12px; }
.schedule { display:grid; grid-template-columns:repeat(auto-fill,minmax(min(290px,100%),1fr)); gap:14px; align-items:start; }
.channel { --band:hsl(var(--h) 70% var(--band-l)); background:var(--card); border-radius:10px; border-top:5px solid var(--band); box-shadow:0 1px 3px rgb(0 0 0 / .08); overflow:hidden; break-inside:avoid; }
.channel header { padding:9px 14px; background:hsl(var(--h) 70% var(--tint-l)); }
.channel h3 { margin:0; font-size:1.05em; color:hsl(var(--h) 60% var(--ink-l)); }
.channel .id { font-weight:400; color:var(--muted); font-size:.8em; margin-left:6px; }
.channel .date { display:block; color:var(--muted); font-size:.82em; font-variant-numeric:tabular-nums; }
.programmes { position:relative; list-style:none; margin:0; padding:4px 0; max-height:26rem; overflow-y:auto; }
.programmes li { display:flex; gap:12px; padding:6px 14px; border-bottom:1px solid var(--border); }
.programmes li:last-child { border-bottom:0; }
.programmes li.past { opacity:.45; }
.programmes li.now { background:var(--now); box-shadow:inset 4px 0 0 var(--band); }
.time { flex:none; width:3.2em; font-variant-numeric:tabular-nums; color:var(--muted); font-size:14px; padding-top:1px; }
li.now .time { color:var(--fg); font-weight:700; }
.title { font-weight:600; }
.cats { display:flex; gap:4px; flex-wrap:wrap; margin-top:3px; }
.cat { padding:0 8px; border-radius:999px; font-size:12px; background:hsl(var(--h) 70% var(--tint-l)); color:hsl(var(--h) 55% var(--ink-l)); }
.empty { margin:0 20px 24px; color:var(--muted); }
.empty summary { cursor:pointer; }
@media print {
  .top { position:static; backdrop-filter:none; background:none; border:0; padding:0 0 8px; }
  .controls, .bar button { display:none; }
  body { background:#fff; color:#000; font-size:10pt; -webkit-print-color-adjust:exact; print-color-adjust:exact; }
  main { padding:0; }
  .day[hidden] { display:block !important; }
  .day + .day { margin-top:0; break-before:page; }
  .schedule { grid-template-columns:1fr 1fr; gap:8px; }
  .channel { box-shadow:none; border:1px solid #ccc; border-top:5px solid var(--band); }
  .programmes { max-height:none; overflow:visible; }
  .programmes li.past { opacity:1; }
  .programmes li.now { background:none; box-shadow:none; }
}
)CSS";

// Shows the day holding the current programme (or the URL's day), highlights what is on,
// filters channels by name, and keeps the highlight fresh. Without it every day is shown.
constexpr std::string_view kScript = R"JS(
(function () {
  var days = Array.prototype.slice.call(document.querySelectorAll("section.day"));
  var tabs = Array.prototype.slice.call(document.querySelectorAll("a.tab"));
  var filter = document.getElementById("filter");
  var now = function () { return Date.now() / 1000; };

  function refresh() {
    var t = now();
    document.querySelectorAll("ol.programmes").forEach(function (list) {
      var current = null;
      Array.prototype.forEach.call(list.children, function (li) {
        var on = +li.dataset.start <= t && t < +li.dataset.stop;
        li.classList.toggle("past", +li.dataset.stop <= t);
        li.classList.toggle("now", on);
        if (on) current = li;
      });
      if (current && list.offsetParent) list.scrollTop = Math.max(0, current.offsetTop - 8);
    });
  }

  function show(id) {
    days.forEach(function (d) { d.hidden = d.id !== id; });
    tabs.forEach(function (a) {
      if (a.hash === "#" + id) a.setAttribute("aria-current", "page");
      else a.removeAttribute("aria-current");
    });
    refresh();
  }

  tabs.forEach(function (a) {
    a.addEventListener("click", function (e) {
      e.preventDefault();
      history.replaceState(null, "", a.hash);
      show(a.hash.slice(1));
      window.scrollTo(0, 0);
    });
  });

  filter.addEventListener("input", function () {
    var q = filter.value.trim().toLowerCase();
    document.querySelectorAll("section.channel").forEach(function (c) {
      c.hidden = q !== "" && c.dataset.name.toLowerCase().indexOf(q) < 0;
    });
  });

  var t = now();
  var fromUrl = location.hash && document.getElementById(location.hash.slice(1));
  var onAir = days.filter(function (d) {
    return Array.prototype.some.call(d.querySelectorAll("ol.programmes li"), function (li) {
      return +li.dataset.start <= t && t < +li.dataset.stop;
    });
  })[0];
  var initial = fromUrl || onAir || days[0];
  if (initial) show(initial.id);
  setInterval(refresh, 60000);
})();
)JS";

std::string escape(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += ch;
        }
    }
    return out;
}

// A stable hue (0-359) for a name, so a channel or category keeps its colour.
int hue(std::string_view text) {
    std::uint32_t h = 2166136261u;  // FNV-1a
    for (const char ch : text) {
        h ^= static_cast<unsigned char>(ch);
        h *= 16777619u;
    }
    return static_cast<int>(h % 360);
}

// Formats the wall-clock time in `zone` by shifting to local time and formatting as UTC.
std::string format_local(Time t, const TimeZone& zone, const char* pattern) {
    const Time local = t + utc_offset(zone, t);
    const auto system_time = std::chrono::system_clock::time_point{local.time_since_epoch()};
    const std::time_t tt = std::chrono::system_clock::to_time_t(system_time);
    const std::tm tm = *std::gmtime(&tt);
    std::ostringstream out;
    out << std::put_time(&tm, pattern);
    return out.str();
}

using Day = std::chrono::sys_days;  // a calendar date in the display zone

Day local_day(Time t, const TimeZone& zone) {
    return std::chrono::floor<std::chrono::days>(t + utc_offset(zone, t));
}

// Wall-clock time since local midnight in `zone`.
std::chrono::minutes local_time_of_day(Time t, const TimeZone& zone) {
    const Time local = t + utc_offset(zone, t);
    return std::chrono::duration_cast<std::chrono::minutes>(local - std::chrono::floor<std::chrono::days>(local));
}

// The broadcast day a programme belongs to: the local day it starts, or the day before
// when it starts before the channel's morning time.
Day broadcast_day(Time t, std::chrono::minutes morning, const TimeZone& zone) {
    return std::chrono::floor<std::chrono::days>(t + utc_offset(zone, t) - morning);
}

// `day` is already a local date, so these format it without a further shift.
std::string day_label(Day day) { return format_local(day, utc_zone(), "%A %d %B %Y"); }  // "Thursday 29 October 2026"
std::string tab_label(Day day) { return format_local(day, utc_zone(), "%a %d %b"); }     // "Thu 29 Oct"
std::string day_id(Day day) { return format_local(day, utc_zone(), "day-%Y-%m-%d"); }

// The span a channel's box covers, from its first start to its last stop:
// "Thu 29 Oct 06:00–23:00", or with the end day named when it differs,
// "Thu 29 Oct 06:00 – Fri 30 Oct 06:00".
std::string date_range(const std::vector<const Programme*>& programmes, Day day,
                       const TimeZone& zone) {
    Time last_stop = programmes.front()->stop;
    for (const Programme* p : programmes) last_stop = std::max(last_stop, p->stop);

    std::string out = tab_label(day) + ' ' + format_local(programmes.front()->start, zone, "%H:%M");
    if (local_day(last_stop, zone) == day) return out + "–" + format_local(last_stop, zone, "%H:%M");
    return out + " – " + format_local(last_stop, zone, "%a %d %b %H:%M");
}

std::string display_name(const Guide& guide, const std::string& id) {
    for (const auto& channel : guide.channels) {
        if (channel.id == id && !channel.display_names.empty()) return channel.display_names.front();
    }
    return id;
}

bool is_listed(const Guide& guide, const std::string& id) {
    for (const auto& channel : guide.channels) {
        if (channel.id == id) return true;
    }
    return false;
}

void render_channel(std::ostringstream& out, const Guide& guide, const std::string& id,
                    const std::vector<const Programme*>& programmes, Day day,
                    const TimeZone& zone) {
    const std::string name = display_name(guide, id);
    out << "<section class=\"channel\" style=\"--h:" << hue(id) << "\" data-name=\""
        << escape(name + ' ' + id) << "\">\n<header><h3>" << escape(name)
        << " <span class=\"id\">" << escape(id) << "</span>";
    if (!is_listed(guide, id)) out << " <span class=\"id\">(not in channel list)</span>";
    out << "</h3><span class=\"date\">" << date_range(programmes, day, zone)
        << "</span></header>\n<ol class=\"programmes\">\n";

    for (const Programme* p : programmes) {
        out << "<li data-start=\"" << p->start.time_since_epoch().count() << "\" data-stop=\""
            << p->stop.time_since_epoch().count() << "\"><span class=\"time\">"
            << format_local(p->start, zone, "%H:%M") << "</span><div><div class=\"title\">"
            << escape(p->title) << "</div>";
        if (!p->categories.empty()) {
            out << "<div class=\"cats\">";
            for (const auto& category : p->categories) {
                out << "<span class=\"cat\" style=\"--h:" << hue(category) << "\">"
                    << escape(category) << "</span>";
            }
            out << "</div>";
        }
        out << "</div></li>\n";
    }
    out << "</ol>\n</section>\n";
}

}  // namespace

std::string render_schedule_html(const Guide& guide, const TimeZone& zone) {
    std::vector<std::string> ids;
    std::set<std::string> seen;
    for (const auto& channel : guide.channels) {
        if (seen.insert(channel.id).second) ids.push_back(channel.id);
    }
    for (const auto& p : guide.programmes) {
        if (seen.insert(p.channel_id).second) ids.push_back(p.channel_id);
    }

    // A channel's morning time is the time of day its first programme in the guide starts,
    // capped at 06:00 so a guide that begins in the evening doesn't pull the next day's
    // daytime programmes into the previous day.
    constexpr std::chrono::minutes kLatestMorning = std::chrono::hours{6};
    std::map<std::string, Time> first_start;
    for (const auto& p : guide.programmes) {
        const auto [it, inserted] = first_start.try_emplace(p.channel_id, p.start);
        if (!inserted) it->second = std::min(it->second, p.start);
    }
    std::map<std::string, std::chrono::minutes> morning;
    for (const auto& [id, start] : first_start) {
        morning[id] = std::min(local_time_of_day(start, zone), kLatestMorning);
    }

    // Day -> channel -> programmes, each programme filed under its broadcast day, which runs
    // from the channel's morning time to the next morning. Programmes are already sorted by
    // start within a channel, so lists stay in order.
    std::map<Day, std::map<std::string, std::vector<const Programme*>>> by_day;
    for (const auto& p : guide.programmes) {
        by_day[broadcast_day(p.start, morning[p.channel_id], zone)][p.channel_id].push_back(&p);
    }

    std::ostringstream out;
    out << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"utf-8\">\n"
           "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
           "<title>Schedule · tvguide</title>\n<style>"
        << kStyle << "</style>\n</head>\n<body>\n"
           "<header class=\"top\">\n<div class=\"bar\"><h1>Schedule</h1>"
        << "<p class=\"lead\">Times are " << escape(zone.label);
    if (zone.eu_summer_time) {
        out << " (" << escape(zone.standard_abbrev) << '/' << escape(zone.summer_abbrev) << ')';
    }
    out << ".</p><button onclick=\"window.print()\">Print</button></div>\n"
           "<div class=\"controls\">\n<nav class=\"tabs\" aria-label=\"Days\">";
    for (const auto& [day, channels] : by_day) {
        out << "<a class=\"tab\" href=\"#" << day_id(day) << "\">" << tab_label(day) << "</a>";
    }
    out << "</nav>\n<input id=\"filter\" type=\"search\" placeholder=\"Filter channels\" "
           "aria-label=\"Filter channels\">\n</div>\n</header>\n<main>\n";

    for (const auto& [day, channels] : by_day) {
        out << "<section class=\"day\" id=\"" << day_id(day) << "\">\n<h2>" << day_label(day)
            << "</h2>\n<div class=\"schedule\">\n";
        for (const auto& id : ids) {
            if (const auto it = channels.find(id); it != channels.end()) {
                render_channel(out, guide, id, it->second, day, zone);
            }
        }
        out << "</div>\n</section>\n";
    }
    out << "</main>\n";

    std::vector<std::string> idle;
    for (const auto& id : ids) {
        if (!first_start.contains(id)) idle.push_back(escape(display_name(guide, id)));
    }
    if (!idle.empty()) {
        out << "<details class=\"empty\"><summary>" << idle.size()
            << (idle.size() == 1 ? " channel" : " channels") << " without programmes</summary>\n"
            << "<p>No programmes: ";
        for (std::size_t i = 0; i < idle.size(); ++i) out << (i ? ", " : "") << idle[i];
        out << "</p>\n</details>\n";
    }

    out << "<script>" << kScript << "</script>\n</body>\n</html>\n";
    return out.str();
}

}  // namespace xmltv
