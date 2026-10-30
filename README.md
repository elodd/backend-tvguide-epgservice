# tvguide

[![tests](https://github.com/elodd/backend-tvguide-epgservice/actions/workflows/tests.yml/badge.svg)](https://github.com/elodd/backend-tvguide-epgservice/actions/workflows/tests.yml)

A C++20 library and command-line tool that turn XMLTV electronic programme guides into printable HTML schedules.

## Features

- Parses XMLTV files into plain data types (`Channel`, `Programme`, `Guide`)
- Timestamps with UTC offsets are normalised to UTC
- Programmes are sorted by channel and start time, so each channel's schedule is grouped together
- Printable HTML schedule output, one section per day with one box per channel, in Swedish time (CET/CEST) or UTC
- No global state; the `xmltv` library parses strings and does no I/O beyond that
- Optional `xmltv_fetch` library (`include/xmltv/fetch.hpp`) for downloading (libcurl) and gunzipping (zlib) guides

## Requirements

- C++20 compiler (GCC 13+ or Clang 16+)
- CMake 3.23+
- Git (pugixml, doctest and trompeloeil are fetched automatically via CMake `FetchContent`)
- libcurl and zlib (part of the macOS SDK; `libcurl4-openssl-dev` and `zlib1g-dev` on Debian/Ubuntu)

## Dependencies

| Dependency | Version | Used by | Purpose | How it is obtained | Licence |
|---|---|---|---|---|---|
| [pugixml](https://github.com/zeux/pugixml) | 1.14 | `xmltv` | XML parsing | CMake `FetchContent` (pinned tag `v1.14`) | MIT |
| [doctest](https://github.com/doctest/doctest) | 2.4.11 | `tests` | Unit tests | CMake `FetchContent` (pinned tag `v2.4.11`), only with `TVGUIDE_BUILD_TESTS=ON` | MIT |
| [trompeloeil](https://github.com/rollbear/trompeloeil) | v49 | `tests` | Mocking (`MockDownloader` stands in for the network) | CMake `FetchContent` (pinned tag `v49`), only with `TVGUIDE_BUILD_TESTS=ON`; header-only, used through its doctest adapter `<doctest/trompeloeil.hpp>` | Boost Software License 1.0 |
| [libcurl](https://curl.se/libcurl/) | system | `xmltv_fetch` | Downloading guides (`--epgshare`, `--url`) | System package, found with `find_package(CURL)` | curl (MIT-style) |
| [zlib](https://zlib.net/) | system | `xmltv_fetch` | Inflating `.xml.gz` guides | System package, found with `find_package(ZLIB)` | zlib |

The parser library `xmltv` depends only on pugixml and the C++ standard library, so it can be used without libcurl and zlib. Those two are linked privately into `xmltv_fetch`, which the `tvguide` CLI and the tests use.

The generated `schedule.html` has no runtime dependencies: its CSS and JavaScript are inline, and it loads nothing from the network.

`scripts/download-epg.sh` needs `bash`, `curl` and `gunzip`, which macOS and most Linux distributions ship with.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

To build without the tests: `cmake -S . -B build -DTVGUIDE_BUILD_TESTS=OFF`.

## Test

```sh
cmake --build build --target tests
ctest --test-dir build --output-on-failure
```

## Usage

```sh
tvguide --epgshare UK1 --html out/
```

This writes `out/schedule.html`, a self-contained page with inline CSS and JavaScript and a print layout. A sticky bar has one tab per day and a filter box that narrows the channels by name; the page opens on the day that is on air now. Each day holds one colour-coded box per channel, headed by the channel and the span of its programmes (e.g. "BBC One Lon HD", "Thu 29 Oct 01:10 – Fri 30 Oct 01:10"), listing start time, title and colour-coded categories. The programme on air is highlighted and earlier ones are dimmed. Times are in Swedish time (CET/CEST, `Europe/Stockholm`); pass `--tz UTC` to keep UTC, which matches UK time in winter. With `--tz UTC`, BBC One London's Thursday evening reads 18:00 BBC News, 19:00 The One Show, 19:30 EastEnders, 20:00 The Celebrity Traitors. The output directory is created if it does not exist. Days are broadcast days: a channel's day runs from its morning time (the time of day of its first programme in the guide, at the latest 06:00) to the next morning, so programmes after midnight stay in the previous day's box. Channels with no programmes are listed in a collapsed section at the end. Printing shows every day, each starting on a new page, in two columns.

`--epgshare UK1` downloads https://epgshare01.online/epgshare01/epg_ripper_UK1.xml.gz, the UK guide with BBC One, ITV1, Channel 4 and the other UK channels. A guide can also be read from a local file or any URL:

```sh
tvguide --file tests/data/sample.xml --html out/
tvguide --url <guide-url> --html out/       # plain or gzipped XMLTV
```

To keep a local copy instead (handy when iterating on the parser or HTML without re-downloading), use the download script. It saves `data/epg_<code>.xml`, which is gitignored:

```sh
scripts/download-epg.sh                  # UK1 by default; several codes can be given; --keep-gz also keeps the .xml.gz
tvguide --file data/epg_UK1.xml --html out/
```

`--epgshare` takes an epgshare01.online guide code (letters, digits, underscores). Gzipped input is detected by its magic bytes, so `--file` also accepts `.xml.gz`.

Exactly one of `--file`, `--epgshare` or `--url` is required, plus `--html`. `--tz` is optional: `Europe/Stockholm` (default, alias `sweden`) or `UTC`. Exit codes: `0` on success, `1` if the input cannot be read or parsed or the output cannot be written, `2` for missing or unknown arguments.

The HTML is generated by `xmltv::render_schedule_html()`, so it can also be called from your own code.

## Library example

```cpp
#include <xmltv/html.hpp>
#include <xmltv/xmltv.hpp>

auto result = xmltv::parse(xml_text);
if (auto* err = std::get_if<xmltv::Error>(&result)) {
    std::cerr << err->message << '\n';
    return 1;
}
const auto html = xmltv::render_schedule_html(std::get<xmltv::Guide>(result), xmltv::stockholm_zone());
```

To download, inflate and parse in one call, use `load_guide()` from `xmltv_fetch`. It takes any `xmltv::Downloader`, so tests can pass a mock instead of `CurlDownloader`:

```cpp
#include <xmltv/fetch.hpp>

xmltv::CurlDownloader downloader;
auto result = xmltv::load_guide("https://epgshare01.online/epgshare01/epg_ripper_UK1.xml.gz", downloader);
```

## Implemented

- Parsing of `<channel>` (id, display names) and `<programme>` (channel, start, stop, title, description, categories)
- Timestamp parsing for `YYYYMMDDHHMM[SS] [+-HHMM]`, normalised to UTC
- Validation: missing attributes, bad timestamps, stop before start, malformed XML; zero-length programmes (stop equal to start) are skipped
- `tvguide` CLI with `--file`, `--epgshare <code>`, `--url` and `--html <dir>`, writing a printable `schedule.html` with one box per channel per day, HTML-escaped
- Unlisted channels (programmes without a matching `<channel>`) get their own box marked "not in channel list"
- doctest suite covering timestamps, parsing, HTML output, and errors
- `Downloader` interface (`CurlDownloader` for real downloads) and `load_guide()`, so the download, gunzip and parse pipeline is tested with a trompeloeil mock and no network

## Extendable

- **More XMLTV fields**: `<credits>`, `<episode-num>`, `<rating>`, `<icon>`, and `<url>` can be added to `Programme` in `include/xmltv/xmltv.hpp` and read in `src/parse.cpp`.
- **Other views**: a per-time-slot grid can be added in `src/html.cpp` next to the per-day, per-channel boxes.
- **Output formats**: the HTML renderer in `src/html.cpp` is one example. A JSON writer can sit beside it, with the CLI choosing between them in `apps/tvguide.cpp`.
- **Other guide sources**: a new parser can produce the same `Guide` type from other formats, since everything downstream depends only on the model in the header.
- **More time zones**: `include/xmltv/timezone.hpp` covers UTC and Sweden with a built-in EU summer-time rule, because Apple's libc++ has no `std::chrono::locate_zone`/`zoned_time`. Other EU zones are one `TimeZone` entry each; zones with other rules would need a tz database (e.g. Howard Hinnant's `date/tz.h`).
- **Streaming large files**: `parse()` loads the whole document into memory with pugixml. A pull parser can replace it if guides grow too large.

## Layout

```
include/xmltv/   public API: xmltv.hpp (model, parse), html.hpp (schedule page), timezone.hpp (UTC, Swedish time), fetch.hpp (download)
src/             parse.cpp (XML to model), timestamp.cpp, timezone.cpp, html.cpp, fetch.cpp (download, gunzip)
apps/            tvguide.cpp, the command-line tool
tests/           test_xmltv.cpp (doctest) and data/sample.xml[.gz]
scripts/         download-epg.sh (fetch epgshare01 guides into data/)
```

## Notes

- Programmes that reference a channel missing from the `<channel>` list are kept, since real feeds often have this.
- Malformed programmes (missing `start`/`stop`, bad timestamps, stop before start) are reported as errors. Zero-length programmes are dropped silently, since some feeds use them as placeholders.
- Full XMLTV files are not committed; download your own guide (e.g. `scripts/download-epg.sh`) and pass it with `--file`, or use `--epgshare`/`--url`.
- `CMAKE_POLICY_VERSION_MINIMUM` is set in `CMakeLists.txt` because doctest declares an old CMake minimum that CMake 4 rejects.
