#include "positions/tags.hpp"

#include <algorithm>
#include <iostream>

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
    const auto split = split_tags(tags);
    return std::ranges::any_of(split, [tag](std::string_view current) {
        return current == tag;
    });
}

std::string_view mobility_bucket(int legal_move_count) noexcept {
    if (legal_move_count <= 3) {
        return "low";
    }
    if (legal_move_count >= 9) {
        return "high";
    }
    return "normal";
}

void check_tag_consistency(std::string_view position_name, std::string_view tags,
                           std::string_view tag, bool expected, int& warning_count) {
    const auto actual = has_tag(tags, tag);
    if (actual == expected) {
        return;
    }

    ++warning_count;
    std::cout << "  warning: tag '" << tag << "' is " << (actual ? "present" : "missing")
              << " but computed value is " << (expected ? "true" : "false") << " for "
              << position_name << '\n';
}

} // namespace othello::benchmarks
