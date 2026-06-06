#pragma once

#include "opening_suite.hpp"
#include "player_spec.hpp"
#include "types.hpp"

#include <cstdint>
#include <optional>
#include <othello/othello.hpp>
#include <random>
#include <span>
#include <utility>
#include <vector>

namespace othello::match_runner {

class InProcessPlayer {
public:
    explicit InProcessPlayer(PlayerSpec spec);

    void reset_for_new_game() noexcept;
    [[nodiscard]] MoveSelection choose_move(const Board& board, std::mt19937_64& rng);
    [[nodiscard]] std::uint32_t search_session_generation() const noexcept;

private:
    PlayerSpec spec_;
    SearchSession search_session_;
};

[[nodiscard]] MoveSelection choose_move(const PlayerSpec& spec, const Board& board,
                                        std::mt19937_64& rng);
[[nodiscard]] std::pair<int, int> final_scores(const Board& board) noexcept;
[[nodiscard]] GameRecord run_game(int game_index, const PlayerSpec& black_spec,
                                  const PlayerSpec& white_spec, bool black_is_player_a,
                                  std::uint64_t seed, int opening_index, const Opening& opening);
[[nodiscard]] GameRecord run_game(int game_index, const PlayerSpec& black_spec,
                                  const PlayerSpec& white_spec, bool black_is_player_a,
                                  std::uint64_t seed, int opening_index, const Opening& opening,
                                  std::span<const ExternalEngineConfig> external_engines,
                                  int external_timeout_ms);
[[nodiscard]] GameRecord run_game(int game_index, const PlayerSpec& black_spec,
                                  const PlayerSpec& white_spec, bool black_is_player_a,
                                  std::uint64_t seed);
[[nodiscard]] std::size_t opening_index_for_game(int game_index, bool swap_sides,
                                                 std::size_t opening_count) noexcept;
[[nodiscard]] std::vector<GameRecord> run_match(const MatchConfig& config);
[[nodiscard]] MatchSummary summarize(std::span<const GameRecord> records);

} // namespace othello::match_runner
