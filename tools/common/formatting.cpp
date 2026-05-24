#include "common/formatting.hpp"

namespace othello::tools {

std::string format_square(std::optional<Square> square) {
    if (!square.has_value()) {
        return "-";
    }
    return to_string(*square);
}

std::string format_moves(Bitboard moves) {
    std::string text;
    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const auto square = Square::from_index(index);
        if (!square.has_value() || (moves & square->bit()) == 0) {
            continue;
        }
        if (!text.empty()) {
            text += ' ';
        }
        text += to_string(*square);
    }
    return text.empty() ? "-" : text;
}

std::string format_principal_variation(const std::vector<Square>& principal_variation) {
    std::string text;
    for (const Square square : principal_variation) {
        if (!text.empty()) {
            text += "->";
        }
        text += to_string(square);
    }
    return text.empty() ? "-" : text;
}

double elapsed_ms(std::chrono::nanoseconds elapsed) noexcept {
    return std::chrono::duration<double, std::milli>{elapsed}.count();
}

double ns_per_call(std::chrono::nanoseconds elapsed, std::uint64_t calls) noexcept {
    if (calls == 0) {
        return 0.0;
    }
    return static_cast<double>(elapsed.count()) / static_cast<double>(calls);
}

double calls_per_second(std::chrono::nanoseconds elapsed, std::uint64_t calls) noexcept {
    const double seconds = std::chrono::duration<double>{elapsed}.count();
    if (seconds == 0.0) {
        return 0.0;
    }
    return static_cast<double>(calls) / seconds;
}

} // namespace othello::tools
