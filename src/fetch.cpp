#include "xmltv/fetch.hpp"

#include <curl/curl.h>
#include <zlib.h>

#include <algorithm>
#include <memory>
#include <utility>
#include <variant>

namespace xmltv {
namespace {

size_t append_body(char* data, size_t size, size_t count, void* user) {
    static_cast<std::string*>(user)->append(data, size * count);
    return size * count;
}

}  // namespace

Result<std::string> epgshare01_url(std::string_view code) {
    const bool valid = !code.empty() && std::all_of(code.begin(), code.end(), [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
               c == '_';
    });
    if (!valid) {
        return Error{ErrorKind::BadSource, "invalid epgshare01 code '" + std::string(code) + "'"};
    }
    return "https://epgshare01.online/epgshare01/epg_ripper_" + std::string(code) + ".xml.gz";
}

bool is_gzip(std::string_view bytes) {
    return bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0x1f &&
           static_cast<unsigned char>(bytes[1]) == 0x8b;
}

Result<std::string> gunzip(std::string_view bytes) {
    z_stream stream{};
    // 16 + MAX_WBITS: expect a gzip header rather than raw zlib.
    if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) {
        return Error{ErrorKind::Decompress, "cannot initialise zlib"};
    }
    const std::unique_ptr<z_stream, int (*)(z_stream*)> guard(&stream, inflateEnd);

    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(bytes.data()));
    stream.avail_in = static_cast<uInt>(bytes.size());

    std::string out;
    char chunk[64 * 1024];
    int status = Z_OK;
    while (status != Z_STREAM_END) {
        stream.next_out = reinterpret_cast<Bytef*>(chunk);
        stream.avail_out = sizeof chunk;
        status = inflate(&stream, Z_NO_FLUSH);
        if (status != Z_OK && status != Z_STREAM_END) {
            return Error{ErrorKind::Decompress,
                         std::string("gzip: ") + (stream.msg ? stream.msg : "truncated data")};
        }
        out.append(chunk, sizeof chunk - stream.avail_out);
    }
    return out;
}

Result<std::string> download(const std::string& url) {
    const std::unique_ptr<CURL, void (*)(CURL*)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) return Error{ErrorKind::Download, "cannot initialise libcurl"};

    std::string body;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT, 300L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "tvguide/0.1");
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &body);

    if (const CURLcode code = curl_easy_perform(curl.get()); code != CURLE_OK) {
        long status = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
        if (status >= 400) {
            return Error{ErrorKind::Download, url + ": HTTP " + std::to_string(status)};
        }
        return Error{ErrorKind::Download, url + ": " + curl_easy_strerror(code)};
    }
    return body;
}

Result<std::string> CurlDownloader::get(const std::string& url) { return download(url); }

Result<Guide> parse_guide(std::string bytes) {
    if (is_gzip(bytes)) {
        auto inflated = gunzip(bytes);
        if (auto* err = std::get_if<Error>(&inflated)) return *err;
        bytes = std::move(std::get<std::string>(inflated));
    }
    return parse(bytes);
}

Result<Guide> load_guide(const std::string& url, Downloader& downloader) {
    auto raw = downloader.get(url);
    if (auto* err = std::get_if<Error>(&raw)) return *err;
    return parse_guide(std::move(std::get<std::string>(raw)));
}

}  // namespace xmltv
