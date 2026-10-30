#include "xmltv/fetch.hpp"
#include "xmltv/html.hpp"
#include "xmltv/timezone.hpp"
#include "xmltv/xmltv.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <variant>

namespace {

void print_usage(std::ostream& out) {
    out << "usage: tvguide (--file <guide.xml[.gz]> | --epgshare <code> | --url <url>) --html <dir>\n"
           "               [--tz Europe/Stockholm|UTC]\n"
           "  --file      XMLTV file to read, plain or gzipped\n"
           "  --epgshare  download an epgshare01.online guide by code, e.g. HU1\n"
           "  --url       download an XMLTV guide, plain or gzipped\n"
           "  --html      directory to write schedule.html into (one box per channel)\n"
           "  --tz        time zone for the schedule: Europe/Stockholm (default, CET/CEST) or UTC\n";
}

xmltv::Result<std::string> read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return xmltv::Error{xmltv::ErrorKind::BadSource, "cannot open " + path};
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

}  // namespace

int main(int argc, char** argv) {
    std::string file;
    std::string epgshare;
    std::string url;
    std::string html_dir;
    std::string tz = "Europe/Stockholm";

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool has_value = i + 1 < argc;
        if (arg == "--file" && has_value) {
            file = argv[++i];
        } else if (arg == "--epgshare" && has_value) {
            epgshare = argv[++i];
        } else if (arg == "--url" && has_value) {
            url = argv[++i];
        } else if (arg == "--html" && has_value) {
            html_dir = argv[++i];
        } else if (arg == "--tz" && has_value) {
            tz = argv[++i];
        } else {
            print_usage(std::cerr);
            return 2;
        }
    }

    const int sources = !file.empty() + !epgshare.empty() + !url.empty();
    if (sources != 1 || html_dir.empty()) {
        print_usage(std::cerr);
        return 2;
    }

    const auto zone = xmltv::find_zone(tz);
    if (!zone) {
        std::cerr << "tvguide: unknown time zone '" << tz << "' (use Europe/Stockholm or UTC)\n";
        return 2;
    }

    if (!epgshare.empty()) {
        auto resolved = xmltv::epgshare01_url(epgshare);
        if (auto* err = std::get_if<xmltv::Error>(&resolved)) {
            std::cerr << "tvguide: " << err->message << '\n';
            return 2;
        }
        url = std::get<std::string>(resolved);
    }

    const auto load = [&]() -> xmltv::Result<xmltv::Guide> {
        if (!url.empty()) {
            std::cout << "downloading " << url << '\n';
            xmltv::CurlDownloader downloader;
            return xmltv::load_guide(url, downloader);
        }
        auto raw = read_file(file);
        if (auto* err = std::get_if<xmltv::Error>(&raw)) return *err;
        return xmltv::parse_guide(std::move(std::get<std::string>(raw)));
    };

    auto parsed = load();
    if (auto* err = std::get_if<xmltv::Error>(&parsed)) {
        std::cerr << "tvguide: " << err->message << '\n';
        return 1;
    }
    const auto& guide = std::get<xmltv::Guide>(parsed);

    std::error_code ec;
    std::filesystem::create_directories(html_dir, ec);
    const auto target = std::filesystem::path(html_dir) / "schedule.html";
    std::ofstream out(target, std::ios::binary);
    if (!out) {
        std::cerr << "tvguide: cannot write " << target.string() << '\n';
        return 1;
    }
    out << xmltv::render_schedule_html(guide, *zone);
    std::cout << "wrote " << target.string() << '\n';
    return 0;
}
