#pragma once

#include <filesystem>
#include <memory>
#include <othello/evaluation_patterns.hpp>
#include <string>
#include <vector>

namespace othello::tools {

struct PatternTableLoadResult {
    std::shared_ptr<const PatternTableBundle> tables;
    std::string error;

    [[nodiscard]] bool ok() const noexcept {
        return error.empty();
    }
};

class PatternTableCache {
public:
    [[nodiscard]] PatternTableLoadResult load(const std::filesystem::path& path);

private:
    struct Entry {
        std::filesystem::path path;
        std::shared_ptr<const PatternTableBundle> tables;
    };

    std::vector<Entry> entries_;
};

[[nodiscard]] PatternTableLoadResult load_pattern_table_file(const std::filesystem::path& path);

} // namespace othello::tools
