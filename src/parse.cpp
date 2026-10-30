#include "xmltv/xmltv.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <tuple>

namespace xmltv {
namespace {

Error missing_attribute(const char* element, const char* attribute) {
    return Error{ErrorKind::MissingAttribute,
                 std::string("<") + element + "> is missing attribute '" + attribute + "'"};
}

}  // namespace

Result<Guide> parse(std::string_view xml) {
    pugi::xml_document doc;
    const auto status = doc.load_buffer(xml.data(), xml.size());
    if (!status) return Error{ErrorKind::MalformedXml, status.description()};

    const pugi::xml_node root = doc.child("tv");
    if (!root) return Error{ErrorKind::MalformedXml, "missing <tv> root element"};

    Guide guide;

    for (const auto node : root.children("channel")) {
        Channel channel;
        channel.id = node.attribute("id").value();
        if (channel.id.empty()) return missing_attribute("channel", "id");
        for (const auto name : node.children("display-name")) {
            channel.display_names.emplace_back(name.child_value());
        }
        guide.channels.push_back(std::move(channel));
    }

    for (const auto node : root.children("programme")) {
        const std::string_view channel_attr = node.attribute("channel").value();
        const std::string_view start_attr = node.attribute("start").value();
        const std::string_view stop_attr = node.attribute("stop").value();
        if (channel_attr.empty()) return missing_attribute("programme", "channel");
        if (start_attr.empty()) return missing_attribute("programme", "start");
        if (stop_attr.empty()) return missing_attribute("programme", "stop");

        auto start = parse_timestamp(start_attr);
        if (auto* err = std::get_if<Error>(&start)) return *err;
        auto stop = parse_timestamp(stop_attr);
        if (auto* err = std::get_if<Error>(&stop)) return *err;

        Programme programme;
        programme.channel_id = std::string(channel_attr);
        programme.start = std::get<Time>(start);
        programme.stop = std::get<Time>(stop);
        // Zero-length entries are placeholders in real feeds; drop them.
        if (programme.stop == programme.start) continue;
        if (programme.stop < programme.start) {
            return Error{ErrorKind::BadTimestamp,
                         "programme on '" + programme.channel_id + "' ends before it starts"};
        }
        programme.title = node.child_value("title");
        programme.description = node.child_value("desc");
        for (const auto category : node.children("category")) {
            programme.categories.emplace_back(category.child_value());
        }
        guide.programmes.push_back(std::move(programme));
    }

    std::sort(guide.programmes.begin(), guide.programmes.end(),
              [](const Programme& a, const Programme& b) {
                  return std::tie(a.channel_id, a.start) < std::tie(b.channel_id, b.start);
              });

    return guide;
}

}  // namespace xmltv
