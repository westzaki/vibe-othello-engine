#include "common/board_io.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

namespace othello::tools {

std::optional<std::string> read_text_file(const std::string& path) {
    std::ifstream input{path};
    if (!input) {
        std::cerr << "failed to open board file: " << path << '\n';
        return std::nullopt;
    }

    std::string text{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    if (!input.good() && !input.eof()) {
        std::cerr << "failed to read board file: " << path << '\n';
        return std::nullopt;
    }
    return text;
}

std::optional<std::string> read_stdin_text() {
    return std::string{std::istreambuf_iterator<char>{std::cin},
                       std::istreambuf_iterator<char>{}};
}

} // namespace othello::tools
