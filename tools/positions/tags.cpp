#include "positions/tags.hpp"

namespace othello::benchmarks {

std::vector<std::string_view> split_tags(std::string_view tags) {
    std::vector<std::string_view> result;
    while (!tags.empty()) {
        const std::size_t comma = tags.find(',');
        const std::string_view tag = tags.substr(0, comma);
        if (!tag.empty()) {
            result.push_back(tag);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        tags.remove_prefix(comma + 1);
    }
    return result;
}

bool has_tag(std::string_view tags, std::string_view tag) {
    for (const std::string_view current : split_tags(tags)) {
        if (current == tag) {
            return true;
        }
    }
    return false;
}

} // namespace othello::benchmarks
