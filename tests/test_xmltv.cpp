#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <doctest/trompeloeil.hpp>

#include "xmltv/fetch.hpp"
#include "xmltv/html.hpp"
#include "xmltv/timezone.hpp"
#include "xmltv/xmltv.hpp"

#include <fstream>
#include <sstream>
#include <string>
#include <variant>

namespace {

namespace c = std::chrono;

xmltv::Time utc(int y, unsigned m, unsigned d, int h, int min) {
    const c::year_month_day ymd{c::year{y}, c::month{m}, c::day{d}};
    const xmltv::Time midnight = c::sys_days{ymd};
    return midnight + c::hours{h} + c::minutes{min};
}

template <class T>
xmltv::ErrorKind kind_of(const xmltv::Result<T>& r) {
    return std::get<xmltv::Error>(r).kind;
}

std::string read_test_file(const std::string& name) {
    std::ifstream in(std::string(XMLTV_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

xmltv::Guide load_sample() {
    auto r = xmltv::parse(read_test_file("sample.xml"));
    REQUIRE(std::holds_alternative<xmltv::Guide>(r));
    return std::get<xmltv::Guide>(r);
}

class MockDownloader : public xmltv::Downloader {
public:
    MAKE_MOCK1(get, xmltv::Result<std::string>(const std::string&), override);
};

constexpr const char* kUk1Url = "https://epgshare01.online/epgshare01/epg_ripper_UK1.xml.gz";

}  // namespace

TEST_CASE("timestamps honour UTC offsets") {
    CHECK(std::get<xmltv::Time>(xmltv::parse_timestamp("20261029180000 +0100")) ==
          utc(2026, 10, 29, 17, 0));
    CHECK(std::get<xmltv::Time>(xmltv::parse_timestamp("20261029180000 -0500")) ==
          utc(2026, 10, 29, 23, 0));
    CHECK(std::get<xmltv::Time>(xmltv::parse_timestamp("20261029180000")) ==
          utc(2026, 10, 29, 18, 0));
}

TEST_CASE("invalid timestamps are rejected") {
    for (const char* text : {"20261332180000", "2026102918", "20261029250000",
                             "20261029180000 0100", "20261029180000 +01"}) {
        CAPTURE(text);
        const auto r = xmltv::parse_timestamp(text);
        CHECK(std::holds_alternative<xmltv::Error>(r));
        CHECK(kind_of(r) == xmltv::ErrorKind::BadTimestamp);
    }
}

TEST_CASE("sample guide parses and is sorted by channel then start") {
    const auto guide = load_sample();
    CHECK(guide.channels.size() == 2);
    REQUIRE(guide.programmes.size() == 4);
    CHECK(guide.programmes[0].channel_id == "hirtv.hu");
    CHECK(guide.programmes[0].title == "Híradó");
    CHECK(guide.programmes[0].categories == std::vector<std::string>{"News"});
    CHECK(guide.programmes[1].title == "Sport");
    CHECK(guide.programmes.back().channel_id == "unknown.tv");
}

TEST_CASE("missing attributes are reported") {
    const auto r = xmltv::parse(
        R"(<tv><programme channel="a" stop="20261029180000"><title>x</title></programme></tv>)");
    REQUIRE(std::holds_alternative<xmltv::Error>(r));
    CHECK(kind_of(r) == xmltv::ErrorKind::MissingAttribute);
}

TEST_CASE("zero-length programmes are skipped, stop before start is an error") {
    const auto r = xmltv::parse(
        R"(<tv>
             <programme channel="a" start="20261029180000" stop="20261029180000"><title>empty</title></programme>
             <programme channel="a" start="20261029180000" stop="20261029190000"><title>kept</title></programme>
           </tv>)");
    REQUIRE(std::holds_alternative<xmltv::Guide>(r));
    const auto& programmes = std::get<xmltv::Guide>(r).programmes;
    REQUIRE(programmes.size() == 1);
    CHECK(programmes.front().title == "kept");

    const auto backwards = xmltv::parse(
        R"(<tv><programme channel="a" start="20261029190000" stop="20261029180000"/></tv>)");
    REQUIRE(std::holds_alternative<xmltv::Error>(backwards));
    CHECK(kind_of(backwards) == xmltv::ErrorKind::BadTimestamp);
}

TEST_CASE("malformed XML is reported") {
    const auto r = xmltv::parse("<tv><channel id=\"a\"");
    REQUIRE(std::holds_alternative<xmltv::Error>(r));
    CHECK(kind_of(r) == xmltv::ErrorKind::MalformedXml);
}

TEST_CASE("schedule html has one box per channel, including unlisted ones") {
    const auto html = xmltv::render_schedule_html(load_sample());
    std::size_t boxes = 0;
    for (std::size_t pos = html.find("class=\"channel\""); pos != std::string::npos;
         pos = html.find("class=\"channel\"", pos + 1)) {
        ++boxes;
    }
    CHECK(boxes == 3);
    CHECK(html.find("HírTV") != std::string::npos);
    CHECK(html.find("(not in channel list)") != std::string::npos);
}

TEST_CASE("schedule html escapes programme text") {
    xmltv::Guide guide;
    xmltv::Programme p;
    p.channel_id = "a<b>";
    p.start = utc(2026, 10, 29, 17, 0);
    p.stop = utc(2026, 10, 29, 18, 0);
    p.title = "Tom & \"Jerry\" <script>";
    guide.programmes.push_back(p);

    const auto html = xmltv::render_schedule_html(guide);
    CHECK(html.find("Tom &amp; &quot;Jerry&quot; &lt;script&gt;") != std::string::npos);
    CHECK(html.find("Tom & \"Jerry\" <script>") == std::string::npos);
    CHECK(html.find("a&lt;b&gt;") != std::string::npos);
}

TEST_CASE("epgshare01 codes map to guide URLs") {
    CHECK(std::get<std::string>(xmltv::epgshare01_url("HU1")) ==
          "https://epgshare01.online/epgshare01/epg_ripper_HU1.xml.gz");
    CHECK(kind_of(xmltv::epgshare01_url("")) == xmltv::ErrorKind::BadSource);
    CHECK(kind_of(xmltv::epgshare01_url("../HU1")) == xmltv::ErrorKind::BadSource);
    CHECK(kind_of(xmltv::epgshare01_url("HU1?x=1")) == xmltv::ErrorKind::BadSource);
}

TEST_CASE("gzipped guides are detected and inflated") {
    std::ifstream in(std::string(XMLTV_TEST_DATA_DIR) + "/sample.xml.gz", std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const std::string gz = buffer.str();
    REQUIRE(xmltv::is_gzip(gz));

    auto inflated = xmltv::gunzip(gz);
    REQUIRE(std::holds_alternative<std::string>(inflated));
    CHECK(std::holds_alternative<xmltv::Guide>(xmltv::parse(std::get<std::string>(inflated))));

    CHECK_FALSE(xmltv::is_gzip("<?xml"));
    CHECK(kind_of(xmltv::gunzip(gz.substr(0, gz.size() / 2))) == xmltv::ErrorKind::Decompress);
}

TEST_CASE("Swedish time follows the EU summer-time switches") {
    const auto se = xmltv::stockholm_zone();
    // 2026: CEST from 29 Mar 01:00 UTC until 25 Oct 01:00 UTC.
    CHECK(xmltv::utc_offset(se, utc(2026, 3, 29, 0, 59)) == std::chrono::hours{1});
    CHECK(xmltv::utc_offset(se, utc(2026, 3, 29, 1, 0)) == std::chrono::hours{2});
    CHECK(xmltv::utc_offset(se, utc(2026, 10, 25, 0, 59)) == std::chrono::hours{2});
    CHECK(xmltv::utc_offset(se, utc(2026, 10, 25, 1, 0)) == std::chrono::hours{1});
    CHECK(xmltv::abbreviation(se, utc(2026, 7, 1, 12, 0)) == "CEST");
    CHECK(xmltv::abbreviation(se, utc(2026, 12, 1, 12, 0)) == "CET");
    // 2027 switches fall on 28 Mar and 31 Oct.
    CHECK(xmltv::in_summer_time(se, utc(2027, 3, 28, 1, 0)));
    CHECK_FALSE(xmltv::in_summer_time(se, utc(2027, 3, 27, 12, 0)));
    CHECK(xmltv::in_summer_time(se, utc(2027, 10, 31, 0, 59)));

    CHECK(xmltv::utc_offset(xmltv::utc_zone(), utc(2026, 7, 1, 12, 0)) == std::chrono::minutes{0});
    CHECK(xmltv::find_zone("Europe/Stockholm").has_value());
    CHECK(xmltv::find_zone("UTC").has_value());
    CHECK_FALSE(xmltv::find_zone("Mars/Olympus").has_value());
}

TEST_CASE("schedule html shows times in the chosen zone") {
    xmltv::Guide guide;
    xmltv::Programme summer;
    summer.channel_id = "svt1.se";
    summer.start = utc(2026, 7, 1, 18, 0);
    summer.stop = utc(2026, 7, 1, 19, 0);
    summer.title = "Summer";
    xmltv::Programme winter = summer;
    winter.start = utc(2026, 12, 1, 18, 0);
    winter.stop = utc(2026, 12, 1, 19, 0);
    winter.title = "Winter";
    guide.programmes = {summer, winter};

    const auto se = xmltv::render_schedule_html(guide, xmltv::stockholm_zone());
    CHECK(se.find("Times are Swedish time (CET/CEST).") != std::string::npos);
    CHECK(se.find("Wednesday 01 July 2026") != std::string::npos);
    CHECK(se.find(">20:00<") != std::string::npos);
    CHECK(se.find("Tuesday 01 December 2026") != std::string::npos);
    CHECK(se.find(">19:00<") != std::string::npos);

    const auto utc_html = xmltv::render_schedule_html(guide);
    CHECK(utc_html.find("Times are UTC.") != std::string::npos);
    CHECK(utc_html.find(">18:00<") != std::string::npos);
}

TEST_CASE("schedule html has one box per channel per broadcast day") {
    xmltv::Guide guide;
    guide.channels = {{"a", {"A"}}, {"b", {"B"}}, {"idle", {"Idle"}}};
    const auto add = [&](const char* ch, xmltv::Time start, xmltv::Time stop) {
        xmltv::Programme p;
        p.channel_id = ch;
        p.start = start;
        p.stop = stop;
        p.title = "x";
        guide.programmes.push_back(p);
    };
    // Winter, so Swedish time is UTC+1. Both channels start later, so their mornings are 06:00.
    add("a", utc(2026, 12, 1, 10, 0), utc(2026, 12, 1, 11, 0));   // 1 Dec 11:00
    add("a", utc(2026, 12, 1, 22, 30), utc(2026, 12, 1, 23, 30)); // 1 Dec 23:30 -> 00:30
    add("a", utc(2026, 12, 1, 23, 30), utc(2026, 12, 2, 0, 30));  // 2 Dec 00:30, still 1 Dec's box
    add("b", utc(2026, 12, 2, 12, 0), utc(2026, 12, 2, 13, 0));   // 2 Dec 13:00

    const auto html = xmltv::render_schedule_html(guide, xmltv::stockholm_zone());
    const auto count = [&](std::string_view needle) {
        std::size_t n = 0;
        for (auto pos = html.find(needle); pos != std::string::npos; pos = html.find(needle, pos + 1)) ++n;
        return n;
    };
    CHECK(count("class=\"day\"") == 2);
    CHECK(count("class=\"tab\"") == 2);
    CHECK(count("class=\"channel\"") == 2);  // a on 1 Dec, b on 2 Dec
    CHECK(html.find("Tuesday 01 December 2026") < html.find("Wednesday 02 December 2026"));
    CHECK(count("<span class=\"date\">Tue 01 Dec 11:00 – Wed 02 Dec 01:30</span>") == 1);
    CHECK(count("<span class=\"date\">Wed 02 Dec 13:00–14:00</span>") == 1);
    CHECK(html.find("No programmes: Idle") != std::string::npos);
}

TEST_CASE("schedule html keeps programmes before a channel's morning time in the previous day's box") {
    xmltv::Guide guide;
    guide.channels = {{"a", {"A"}}, {"b", {"B"}}};
    const auto add = [&](const char* ch, xmltv::Time start, xmltv::Time stop) {
        xmltv::Programme p;
        p.channel_id = ch;
        p.start = start;
        p.stop = stop;
        p.title = "x";
        guide.programmes.push_back(p);
    };
    // UTC, so local times are as written. a starts its guide at 06:00, b at 00:00.
    add("a", utc(2026, 12, 1, 6, 0), utc(2026, 12, 1, 23, 0));
    add("a", utc(2026, 12, 1, 23, 0), utc(2026, 12, 2, 1, 0));
    add("a", utc(2026, 12, 2, 1, 0), utc(2026, 12, 2, 6, 0));    // before a's morning: 1 Dec's box
    add("a", utc(2026, 12, 2, 6, 0), utc(2026, 12, 2, 7, 0));    // a's 2 Dec box
    add("b", utc(2026, 12, 1, 0, 0), utc(2026, 12, 1, 2, 0));
    add("b", utc(2026, 12, 2, 0, 0), utc(2026, 12, 2, 2, 0));    // b's morning is 00:00

    const auto html = xmltv::render_schedule_html(guide);
    const auto count = [&](std::string_view needle) {
        std::size_t n = 0;
        for (auto pos = html.find(needle); pos != std::string::npos; pos = html.find(needle, pos + 1)) ++n;
        return n;
    };
    CHECK(count("class=\"channel\"") == 4);  // a and b on each day
    CHECK(count("<span class=\"date\">Tue 01 Dec 06:00 – Wed 02 Dec 06:00</span>") == 1);
    CHECK(count("<span class=\"date\">Wed 02 Dec 06:00–07:00</span>") == 1);
    CHECK(count("<span class=\"date\">Tue 01 Dec 00:00–02:00</span>") == 1);
    CHECK(count("<span class=\"date\">Wed 02 Dec 00:00–02:00</span>") == 1);

    // Rows carry their UTC start and stop in epoch seconds for the now-highlight.
    const auto epoch = [](xmltv::Time t) { return std::to_string(t.time_since_epoch().count()); };
    CHECK(html.find("data-start=\"" + epoch(utc(2026, 12, 2, 1, 0)) + "\" data-stop=\"" +
                    epoch(utc(2026, 12, 2, 6, 0)) + "\"") != std::string::npos);
}

TEST_CASE("schedule html caps a channel's morning time at 06:00") {
    xmltv::Guide guide;
    const auto add = [&](xmltv::Time start, xmltv::Time stop) {
        xmltv::Programme p;
        p.channel_id = "late";
        p.start = start;
        p.stop = stop;
        p.title = "x";
        guide.programmes.push_back(p);
    };
    // UTC. The guide starts in the evening, so the morning time is capped at 06:00.
    add(utc(2026, 12, 1, 20, 0), utc(2026, 12, 1, 21, 0));
    add(utc(2026, 12, 2, 5, 0), utc(2026, 12, 2, 6, 0));    // before 06:00: 1 Dec's box
    add(utc(2026, 12, 2, 7, 0), utc(2026, 12, 2, 8, 0));    // after 06:00: 2 Dec, not 1 Dec

    const auto html = xmltv::render_schedule_html(guide);
    CHECK(html.find("<span class=\"date\">Tue 01 Dec 20:00 – Wed 02 Dec 06:00</span>") != std::string::npos);
    CHECK(html.find("<span class=\"date\">Wed 02 Dec 07:00–08:00</span>") != std::string::npos);
}

TEST_CASE("load_guide downloads the URL once and parses plain XML") {
    MockDownloader downloader;
    REQUIRE_CALL(downloader, get(kUk1Url)).RETURN(read_test_file("sample.xml"));

    const auto r = xmltv::load_guide(kUk1Url, downloader);
    REQUIRE(std::holds_alternative<xmltv::Guide>(r));
    const auto& guide = std::get<xmltv::Guide>(r);
    const auto sample = load_sample();
    CHECK(guide.channels.size() == sample.channels.size());
    CHECK(guide.programmes.size() == sample.programmes.size());
}

TEST_CASE("load_guide inflates gzipped downloads") {
    MockDownloader downloader;
    REQUIRE_CALL(downloader, get(kUk1Url)).RETURN(read_test_file("sample.xml.gz"));

    const auto r = xmltv::load_guide(kUk1Url, downloader);
    REQUIRE(std::holds_alternative<xmltv::Guide>(r));
    CHECK(std::get<xmltv::Guide>(r).programmes.size() == load_sample().programmes.size());
}

TEST_CASE("load_guide passes download errors through") {
    MockDownloader downloader;
    REQUIRE_CALL(downloader, get(kUk1Url))
        .RETURN(xmltv::Error{xmltv::ErrorKind::Download, std::string(kUk1Url) + ": HTTP 404"});

    const auto r = xmltv::load_guide(kUk1Url, downloader);
    REQUIRE(std::holds_alternative<xmltv::Error>(r));
    CHECK(kind_of(r) == xmltv::ErrorKind::Download);
    CHECK(std::get<xmltv::Error>(r).message.find("HTTP 404") != std::string::npos);
}

TEST_CASE("load_guide reports corrupt gzip and malformed XML") {
    MockDownloader downloader;
    const std::string gz = read_test_file("sample.xml.gz");
    trompeloeil::sequence seq;
    REQUIRE_CALL(downloader, get(kUk1Url)).IN_SEQUENCE(seq).RETURN(gz.substr(0, gz.size() / 2));
    REQUIRE_CALL(downloader, get(kUk1Url)).IN_SEQUENCE(seq).RETURN(std::string("<tv><channel id=\"a\""));

    CHECK(kind_of(xmltv::load_guide(kUk1Url, downloader)) == xmltv::ErrorKind::Decompress);
    CHECK(kind_of(xmltv::load_guide(kUk1Url, downloader)) == xmltv::ErrorKind::MalformedXml);
}
