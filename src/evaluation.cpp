#include <bit>
#include <othello/board.hpp>
#include <othello/evaluation.hpp>
#include <othello/rules.hpp>

namespace othello {
namespace {

constexpr Bitboard a_file = 0x0101010101010101ULL;
constexpr Bitboard h_file = 0x8080808080808080ULL;
constexpr Bitboard not_a_file = ~a_file;
constexpr Bitboard not_h_file = ~h_file;

constexpr Bitboard corner_squares =
    (Bitboard{1} << 0) | (Bitboard{1} << 7) | (Bitboard{1} << 56) | (Bitboard{1} << 63);

constexpr int terminal_score_weight = 1000;

[[nodiscard]] constexpr Board with_side_to_move(const Board& board, Side side) noexcept {
    return Board{
        .black = board.black,
        .white = board.white,
        .side_to_move = side,
    };
}

[[nodiscard]] constexpr Bitboard shift_east(Bitboard bits) noexcept {
    return (bits & not_h_file) << 1;
}

[[nodiscard]] constexpr Bitboard shift_west(Bitboard bits) noexcept {
    return (bits & not_a_file) >> 1;
}

[[nodiscard]] constexpr Bitboard shift_north(Bitboard bits) noexcept {
    return bits << 8;
}

[[nodiscard]] constexpr Bitboard shift_south(Bitboard bits) noexcept {
    return bits >> 8;
}

[[nodiscard]] constexpr Bitboard shift_northeast(Bitboard bits) noexcept {
    return (bits & not_h_file) << 9;
}

[[nodiscard]] constexpr Bitboard shift_northwest(Bitboard bits) noexcept {
    return (bits & not_a_file) << 7;
}

[[nodiscard]] constexpr Bitboard shift_southeast(Bitboard bits) noexcept {
    return (bits & not_h_file) >> 7;
}

[[nodiscard]] constexpr Bitboard shift_southwest(Bitboard bits) noexcept {
    return (bits & not_a_file) >> 9;
}

[[nodiscard]] constexpr Bitboard adjacent_squares(Bitboard bits) noexcept {
    return shift_east(bits) | shift_west(bits) | shift_north(bits) | shift_south(bits) |
           shift_northeast(bits) | shift_northwest(bits) | shift_southeast(bits) |
           shift_southwest(bits);
}

[[nodiscard]] EvaluationPhase phase_for_occupied_count(
    int occupied_count, const EvaluationConfig& config) noexcept {
    if (occupied_count <= config.opening_max_occupied) {
        return EvaluationPhase::Opening;
    }
    if (occupied_count <= config.midgame_max_occupied) {
        return EvaluationPhase::Midgame;
    }
    return EvaluationPhase::Late;
}

[[nodiscard]] constexpr const EvaluationFeatureWeights&
weights_for_phase(const EvaluationConfig& config, EvaluationPhase phase) noexcept {
    switch (phase) {
    case EvaluationPhase::Opening:
        return config.opening;
    case EvaluationPhase::Midgame:
        return config.midgame;
    case EvaluationPhase::Late:
        return config.late;
    }

    return config.opening;
}

[[nodiscard]] int legal_move_count(const Board& board, Side side) noexcept {
    return std::popcount(legal_moves(with_side_to_move(board, side)));
}

[[nodiscard]] int corner_count(const Board& board, Side side) noexcept {
    return std::popcount(board.discs(side) & corner_squares);
}

[[nodiscard]] int legal_corner_move_count(const Board& board, Side side) noexcept {
    return std::popcount(legal_moves(with_side_to_move(board, side)) & corner_squares);
}

[[nodiscard]] int potential_mobility_count(const Board& board, Side side) noexcept {
    const Bitboard empty_squares = ~board.occupied();
    return std::popcount(adjacent_squares(board.discs(opponent(side))) & empty_squares);
}

[[nodiscard]] int frontier_count(const Board& board, Side side) noexcept {
    const Bitboard frontier_squares = adjacent_squares(~board.occupied());
    return std::popcount(board.discs(side) & frontier_squares);
}

[[nodiscard]] int dangerous_x_square_count(const Board& board, Side side) noexcept {
    int count = 0;
    const Bitboard occupied = board.occupied();
    const Bitboard discs = board.discs(side);

    const auto count_if_dangerous = [&](int corner_index, int x_square_index) noexcept {
        const Bitboard corner = Bitboard{1} << corner_index;
        const Bitboard x_square = Bitboard{1} << x_square_index;
        if ((occupied & corner) == 0 && (discs & x_square) != 0) {
            ++count;
        }
    };

    count_if_dangerous(0, 9);   // a1 / b2
    count_if_dangerous(7, 14);  // h1 / g2
    count_if_dangerous(56, 49); // a8 / b7
    count_if_dangerous(63, 54); // h8 / g7
    return count;
}

} // namespace

int evaluate_disc_difference(const Board& board, Side side) noexcept {
    return score(board, side);
}

int evaluate_mobility(const Board& board, Side side) noexcept {
    return legal_move_count(board, side) - legal_move_count(board, opponent(side));
}

EvaluationConfig evaluation_config_for_preset(EvaluationPreset preset) noexcept {
    EvaluationConfig config = default_evaluation_config();
    switch (preset) {
    case EvaluationPreset::Default:
        return config;
    case EvaluationPreset::MobilityPlusSmoke:
        config.opening.mobility = 10;
        config.midgame.mobility = 12;
        config.late.mobility = 8;
        return config;
    }

    return config;
}

std::optional<EvaluationPreset> evaluation_preset_from_name(std::string_view name) noexcept {
    if (name == "default") {
        return EvaluationPreset::Default;
    }
    if (name == "mobility_plus_smoke") {
        return EvaluationPreset::MobilityPlusSmoke;
    }
    return std::nullopt;
}

std::string_view evaluation_preset_name(EvaluationPreset preset) noexcept {
    switch (preset) {
    case EvaluationPreset::Default:
        return "default";
    case EvaluationPreset::MobilityPlusSmoke:
        return "mobility_plus_smoke";
    }

    return "unknown";
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side,
                                             const EvaluationConfig& config) noexcept {
    const int occupied_count = std::popcount(board.occupied());
    const EvaluationPhase phase = phase_for_occupied_count(occupied_count, config);
    const EvaluationFeatureWeights& weights = weights_for_phase(config, phase);

    EvaluationBreakdown breakdown{
        .phase = phase,
        .occupied_count = occupied_count,
        .empty_count = 64 - occupied_count,
        .disc_difference_weight = weights.disc_difference,
        .mobility_weight = weights.mobility,
        .corner_occupancy_weight = weights.corner_occupancy,
        .potential_mobility_weight = weights.potential_mobility,
        .corner_access_weight = weights.corner_access,
        .x_square_danger_weight = weights.x_square_danger,
        .frontier_weight = weights.frontier,
        .terminal_score_weight = terminal_score_weight,
    };

    if (is_game_over(board)) {
        breakdown.terminal = true;
        breakdown.terminal_disc_difference = score(board, side);
        breakdown.terminal_score =
            breakdown.terminal_disc_difference * breakdown.terminal_score_weight;
        breakdown.total = breakdown.terminal_score;
        return breakdown;
    }

    breakdown.disc_difference = evaluate_disc_difference(board, side);
    breakdown.disc_difference_score =
        breakdown.disc_difference * breakdown.disc_difference_weight;

    breakdown.mobility = evaluate_mobility(board, side);
    breakdown.mobility_score = breakdown.mobility * breakdown.mobility_weight;

    breakdown.corner_occupancy = corner_count(board, side) - corner_count(board, opponent(side));
    breakdown.corner_occupancy_score =
        breakdown.corner_occupancy * breakdown.corner_occupancy_weight;

    // Raw feature values are positive when they are good for `side`.
    breakdown.potential_mobility =
        potential_mobility_count(board, side) - potential_mobility_count(board, opponent(side));
    breakdown.potential_mobility_score =
        breakdown.potential_mobility * breakdown.potential_mobility_weight;

    breakdown.corner_access =
        legal_corner_move_count(board, side) - legal_corner_move_count(board, opponent(side));
    breakdown.corner_access_score = breakdown.corner_access * breakdown.corner_access_weight;

    // Owning an X-square next to an empty corner is dangerous, so the diff is
    // opponent dangerous X-squares minus our dangerous X-squares.
    breakdown.x_square_danger = dangerous_x_square_count(board, opponent(side)) -
                                dangerous_x_square_count(board, side);
    breakdown.x_square_danger_score =
        breakdown.x_square_danger * breakdown.x_square_danger_weight;

    // Fewer own frontier discs is usually better.
    breakdown.frontier = frontier_count(board, opponent(side)) - frontier_count(board, side);
    breakdown.frontier_score = breakdown.frontier * breakdown.frontier_weight;

    breakdown.total = breakdown.disc_difference_score + breakdown.mobility_score +
                      breakdown.corner_occupancy_score + breakdown.potential_mobility_score +
                      breakdown.corner_access_score + breakdown.x_square_danger_score +
                      breakdown.frontier_score;
    return breakdown;
}

EvaluationBreakdown evaluate_basic_breakdown(const Board& board, Side side) noexcept {
    return evaluate_basic_breakdown(board, side, default_evaluation_config());
}

int evaluate_with_config(const Board& board, Side side, const EvaluationConfig& config) noexcept {
    return evaluate_basic_breakdown(board, side, config).total;
}

int evaluate_basic(const Board& board, Side side) noexcept {
    return evaluate_with_config(board, side, default_evaluation_config());
}

} // namespace othello
