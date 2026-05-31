#include "position_sampler/position_sampler.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <optional>
#include <othello/notation.hpp>
#include <othello/rules.hpp>
#include <othello/square.hpp>
#include <random>
#include <set>
#include <string>
#include <system_error>
#include <vector>

namespace othello::tools::position_sampler {
namespace {

enum class AttemptOutcome {
    Sampled,
    TerminalBeforeTarget,
    MaxPliesReached,
};

[[nodiscard]] std::vector<Square> legal_squares(const Board& board) {
    std::vector<Square> squares;
    const Bitboard moves = legal_moves(board);
    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const std::optional<Square> square = Square::from_index(index);
        if (square.has_value() && (moves & square->bit()) != 0) {
            squares.push_back(*square);
        }
    }
    return squares;
}

[[nodiscard]] std::optional<Square> choose_random_square(const std::vector<Square>& squares,
                                                         std::mt19937_64& rng) {
    if (squares.empty()) {
        return std::nullopt;
    }
    std::uniform_int_distribution<std::size_t> distribution(0, squares.size() - 1);
    return squares[distribution(rng)];
}

[[nodiscard]] std::optional<Board> play_to_empty_count(int target_empties, int max_plies,
                                                       std::mt19937_64& rng,
                                                       AttemptOutcome& outcome) {
    Board board = Board::initial();
    for (int ply = 0; ply <= max_plies; ++ply) {
        if (empty_count(board) == target_empties) {
            outcome = AttemptOutcome::Sampled;
            return board;
        }

        if (is_game_over(board)) {
            outcome = AttemptOutcome::TerminalBeforeTarget;
            return std::nullopt;
        }

        if (ply == max_plies) {
            break;
        }

        const std::vector<Square> moves = legal_squares(board);
        if (!moves.empty()) {
            const std::optional<Square> square = choose_random_square(moves, rng);
            const std::optional<Board> next = square.has_value() ? apply_move(board, *square)
                                                                 : std::nullopt;
            if (!next.has_value()) {
                outcome = AttemptOutcome::TerminalBeforeTarget;
                return std::nullopt;
            }
            board = *next;
            continue;
        }

        const std::optional<Board> passed = pass_turn(board);
        if (!passed.has_value()) {
            outcome = AttemptOutcome::TerminalBeforeTarget;
            return std::nullopt;
        }
        board = *passed;
    }

    outcome = AttemptOutcome::MaxPliesReached;
    return std::nullopt;
}

[[nodiscard]] std::size_t default_max_attempts(std::size_t count) noexcept {
    return std::max<std::size_t>(1000, count * 1000);
}

[[nodiscard]] bool validate_options(const SamplerOptions& options, std::string& error) {
    if (options.count == 0) {
        error = "count must be positive";
        return false;
    }
    if (options.target_empties.empty()) {
        error = "target empties must not be empty";
        return false;
    }
    for (const int empties : options.target_empties) {
        if (empties < 0 || empties > 64) {
            error = "target empties must be in [0, 64]";
            return false;
        }
    }
    if (options.max_plies < 0) {
        error = "max plies must be non-negative";
        return false;
    }
    return true;
}

} // namespace

int empty_count(const Board& board) noexcept {
    return 64 - std::popcount(board.occupied());
}

std::optional<std::vector<int>> parse_target_empties(std::string_view text, std::string& error) {
    if (text.empty()) {
        error = "--target-empties must not be empty";
        return std::nullopt;
    }

    std::set<int> unique_values;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view token = text.substr(begin, end - begin);
        if (token.empty()) {
            error = "--target-empties contains an empty segment";
            return std::nullopt;
        }

        int value = 0;
        const char* first = token.data();
        const char* last = token.data() + token.size();
        const auto parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last || value < 0 || value > 64) {
            error = "--target-empties values must be integers in [0, 64]";
            return std::nullopt;
        }
        unique_values.insert(value);

        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }

    return std::vector<int>{unique_values.begin(), unique_values.end()};
}

std::optional<std::vector<Board>> sample_positions(const SamplerOptions& options,
                                                   SampleSummary& summary, std::string& error) {
    summary = SampleSummary{};
    if (!validate_options(options, error)) {
        return std::nullopt;
    }

    const std::size_t max_attempts =
        options.max_attempts == 0 ? default_max_attempts(options.count) : options.max_attempts;
    std::mt19937_64 rng{options.seed};
    std::vector<Board> samples;
    samples.reserve(options.count);
    std::set<std::string> seen;

    while (samples.size() < options.count && summary.attempts < max_attempts) {
        const std::size_t target_index = summary.attempts % options.target_empties.size();
        const int target_empties = options.target_empties[target_index];
        ++summary.attempts;

        AttemptOutcome outcome = AttemptOutcome::MaxPliesReached;
        const std::optional<Board> candidate =
            play_to_empty_count(target_empties, options.max_plies, rng, outcome);
        if (!candidate.has_value()) {
            if (outcome == AttemptOutcome::TerminalBeforeTarget) {
                ++summary.discarded_terminal;
            } else {
                ++summary.discarded_max_plies;
            }
            continue;
        }

        if (!options.allow_terminal && is_game_over(*candidate)) {
            ++summary.discarded_terminal;
            continue;
        }

        if (options.unique) {
            const std::string key = to_string(*candidate);
            const auto [_, inserted] = seen.insert(key);
            if (!inserted) {
                ++summary.duplicates;
                continue;
            }
        }

        samples.push_back(*candidate);
    }

    summary.sampled = samples.size();
    if (samples.size() != options.count) {
        error = "unable to sample requested unique positions within attempt budget";
        return std::nullopt;
    }

    return samples;
}

void write_positions(std::ostream& output, std::span<const Board> positions) {
    for (std::size_t index = 0; index < positions.size(); ++index) {
        if (index != 0) {
            output << "\n\n";
        }
        output << to_string(positions[index]);
    }
    if (!positions.empty()) {
        output << '\n';
    }
}

} // namespace othello::tools::position_sampler
