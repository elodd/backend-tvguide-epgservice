#pragma once

#include "xmltv/xmltv.hpp"

#include <string>
#include <string_view>

namespace xmltv {

// URL of an epgshare01.online guide, e.g. "HU1" ->
// https://epgshare01.online/epgshare01/epg_ripper_HU1.xml.gz.
// Codes are letters, digits and underscores; anything else is an error.
Result<std::string> epgshare01_url(std::string_view code);

// True if the bytes start with the gzip magic number.
bool is_gzip(std::string_view bytes);

// Decompresses a gzip stream.
Result<std::string> gunzip(std::string_view bytes);

// Downloads a URL over HTTP(S), following redirects. Non-2xx responses are errors.
Result<std::string> download(const std::string& url);

// Fetches the bytes behind a URL. An interface so the guide pipeline can be tested
// without a network.
class Downloader {
public:
    virtual ~Downloader() = default;
    virtual Result<std::string> get(const std::string& url) = 0;
};

// Downloads with libcurl via download().
class CurlDownloader final : public Downloader {
public:
    Result<std::string> get(const std::string& url) override;
};

// Inflates the bytes if they are gzipped, then parses them as XMLTV.
Result<Guide> parse_guide(std::string bytes);

// Fetches a guide from `url` with `downloader`, then inflates and parses it.
Result<Guide> load_guide(const std::string& url, Downloader& downloader);

}  // namespace xmltv
