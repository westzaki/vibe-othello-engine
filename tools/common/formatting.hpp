#pragma once

#include <chrono>
#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <vector>

namespace othello::tools {

[[nodiscard]] std::string format_square(std::optional<Square> square);
[[nodiscard]] std::string format_moves(Bitboard moves);
[[nodiscard]] std::string format_principal_variation(const std::vector<Square>& principal_variation);
[[nodiscard]] double elapsed_ms(std::chrono::nanoseconds elapsed) noexcept;
[[nodiscard]] double ns_per_call(std::chrono::nanoseconds elapsed, std::uint64_t calls) noexcept;
[[nodiscard]] double calls_per_second(std::chrono::nanoseconds elapsed,
                                      std::uint64_t calls) noexcept;

} // namespace othello::tools
