#pragma once

#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <string_view>
#include <vector>

namespace othello::benchmarks {

struct EndgamePosition {
    std::string_view name;
    int empties = 0;
    std::string_view tags;
    std::string_view board_text;
    std::string_view notes;
    Board board;
    bool smoke = false;
};

[[nodiscard]] inline bool add_endgame_position(std::vector<EndgamePosition>& positions,
                                               std::string_view name, int empties,
                                               std::string_view tags, std::string_view board_text,
                                               std::string_view notes, bool smoke = false) {
    const auto board = board_from_string(board_text);
    if (!board.has_value()) {
        std::cerr << "failed to parse exact endgame benchmark position: " << name << '\n';
        return false;
    }

    positions.push_back(EndgamePosition{.name = name,
                                        .empties = empties,
                                        .tags = tags,
                                        .board_text = board_text,
                                        .notes = notes,
                                        .board = *board,
                                        .smoke = smoke});
    return true;
}

[[nodiscard]] inline std::optional<std::vector<EndgamePosition>> make_endgame_positions() {
    std::vector<EndgamePosition> positions;
    positions.reserve(27);

    if (!add_endgame_position(positions, "one-empty-forced", 1, "low_mobility",
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBW.\n"
                              "side=B",
                              "single legal move on h1", true)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "one-empty-root-pass", 1, "pass,low_mobility",
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBWB.\n"
                              "side=B",
                              "root side passes, white plays h1", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "two-empty-corner-tie", 2, "corner_choice,low_mobility",
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              ".WBBBBW.\n"
                              "side=B",
                              "equal corner scores should pick a1 before h1", true)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "two-empty-search-pass", 2,
                              "pass,edge_heavy,score_lopsided,low_mobility",
                              "WWBBBBBB\n"
                              ".BBWWBBW\n"
                              "B.BWWBWW\n"
                              "BBBWBWBB\n"
                              "BBBBWBBB\n"
                              "BWBBBBWB\n"
                              "BBBBBBBB\n"
                              "BWBBBBBB\n"
                              "side=B",
                              "fixed-seed search benchmark endgame pass position", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "four-empty-forced-edge", 4, "edge_heavy,low_mobility",
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBWW....\n"
                              "side=B",
                              "narrow edge fill sequence", true)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "four-empty-corner-race", 4,
                              "corner_choice,edge_heavy,score_lopsided,low_mobility",
                              ".WWWWWB.\n"
                              "WBWWWBBW\n"
                              "WWWWBBB.\n"
                              "W.WWBBWB\n"
                              "WBWWWWWW\n"
                              "WBWWWWWW\n"
                              "WBBWWWWW\n"
                              "WWWWBWWW\n"
                              "side=B",
                              "reduced variant of the endgame corner-choice fixture", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "six-empty-corner-pressure", 6,
                              "corner_choice,edge_heavy,score_lopsided",
                              ".WWWWWB.\n"
                              ".BWWWBB.\n"
                              "WWWWBBB.\n"
                              "W.WWBBWB\n"
                              "WBWWWWWW\n"
                              "WBWWWWWW\n"
                              "WBBWWWWW\n"
                              "WWWWBWWW\n"
                              "side=B",
                              "corner-choice fixture with six empties", true)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "six-empty-open-edge", 6, "edge_heavy,low_mobility",
                              ".BBBBBB.\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBWBBBBB\n"
                              "BBWW....\n"
                              "side=B",
                              "mostly forced late edge with one interior anchor", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "eight-empty-corner-choice", 8,
            "corner_choice,corner_available,edge_heavy,x_square_risk,score_lopsided,low_mobility",
            ".WWWWWB.\n"
            "..WWWB..\n"
            "WWWWBBB.\n"
            "W.WWBBWB\n"
            "WBWWWWWW\n"
            "WBWWWWWW\n"
            "WBBWWWWW\n"
            "WWWWBWWW\n"
            "side=B",
            "fixed-seed search benchmark endgame corner-choice position", true)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "eight-empty-edge-corridor", 8, "edge_heavy,low_mobility",
                              "....BBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBWBBBB\n"
                              "BBWBBBBB\n"
                              "BBWW....\n"
                              "side=B",
                              "low-branching edge corridor", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "ten-empty-dense-mobility", 10,
            "high_mobility,corner_available,edge_heavy,x_square_risk,score_lopsided",
            "...B....\n"
            "WWBWWW.B\n"
            "WWWWWWWW\n"
            "WBWWWWW.\n"
            "WWWBBWBB\n"
            "WBBBWB.B\n"
            "WWBWBWBB\n"
            "WWWWWWWB\n"
            "side=B",
            "fixed-seed search benchmark dense mobility endgame", true)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "ten-empty-corner-pressure", 10,
                              "corner_choice,corner_available,edge_heavy,score_lopsided",
                              ".WWWWWB.\n"
                              "..WWWB..\n"
                              "WWWWBBB.\n"
                              "W.WWBBWB\n"
                              "WBWWWWWW\n"
                              "WBWWWWWW\n"
                              "WBBWWWWW\n"
                              "WWWWB.W.\n"
                              "side=B",
                              "corner-choice variant with two extra bottom-edge gaps", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "twelve-empty-late-pass-shape", 12, "edge_heavy,score_lopsided",
            "W.WWWWWB\n"
            "...BBBWB\n"
            "WBBWWWWW\n"
            "WBWWWBBB\n"
            "WWWWB...\n"
            "WWWWWB..\n"
            "WWWWWW..\n"
            "WWWWWWW.\n"
            "side=B",
            "derived from fixed-seed late-black-pass by filling two edge squares", true)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "twelve-empty-open-corners", 12,
                              "corner_available,edge_heavy,score_lopsided",
                              ".WBBBBB.\n"
                              ".WBBBBB.\n"
                              ".WBBBBB.\n"
                              ".BBBBBB.\n"
                              "WBBWWBBW\n"
                              "WBBBBBBW\n"
                              ".WBBBBB.\n"
                              ".WBBBBB.\n"
                              "side=W",
                              "symmetric late position with open corner choices", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "fourteen-empty-experimental-pass", 14,
            "experimental_14,pass,edge_heavy,score_lopsided,low_mobility",
            "..WWWWW.\n"
            "...BBBWB\n"
            "WBBWWWWW\n"
            "WBWWWBBB\n"
            "WWWWB...\n"
            "WWWWWB..\n"
            "WWWWWW..\n"
            "WWWWWWW.\n"
            "side=B",
            "fixed-seed late-black-pass fixture kept experimental because it has 14 empties",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "14-empty-low-mobility", 14,
                              "experimental_14,low_mobility,balanced_count,edge_heavy",
                              "WWWWWWWW\n"
                              ".W.WWB.W\n"
                              "BBWWBBBW\n"
                              "..BBBB.W\n"
                              "...BBB.W\n"
                              "BBBBBBBW\n"
                              ".B.B.B.W\n"
                              "BWWWWWWW\n"
                              "side=B",
                              "fixed-seed 14-empty fixture with one legal root move", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "14-empty-normal-mobility", 14,
                              "experimental_14,normal_mobility,balanced_count,edge_heavy",
                              "BBBBBB..\n"
                              "BBBBB...\n"
                              "BBBWW.W.\n"
                              "BBBWWWW.\n"
                              "BBWWWWW.\n"
                              "B.BBWWWW\n"
                              "..B.BBWW\n"
                              ".WWWWWWW\n"
                              "side=B",
                              "fixed-seed 14-empty fixture with four legal root moves", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "14-empty-high-mobility", 14,
            "experimental_14,high_mobility,corner_available,corner_choice,edge_heavy",
            ".WWWW.W.\n"
            "BWBWWWWW\n"
            "BBWWWBW.\n"
            "BBBBBWWW\n"
            "BWBWBBWW\n"
            "BWWBBWWW\n"
            ".W..WBW.\n"
            "...W..W.\n"
            "side=B",
            "fixed-seed 14-empty fixture with fourteen legal root moves", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "14-empty-root-pass", 14,
                              "experimental_14,pass,edge_heavy,score_lopsided",
                              "..BBBBB.\n"
                              "...BBB.W\n"
                              "...BBWWW\n"
                              "W..BWWWW\n"
                              "WBBWWWWW\n"
                              "WB.BWBWW\n"
                              "W.BBWWWW\n"
                              "WWWWWWWW\n"
                              "side=B",
                              "fixed-seed 14-empty fixture where the root side must pass", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "14-empty-opponent-pass-after-move", 14,
            "experimental_14,opponent_pass_after_move,edge_heavy,balanced_count",
            "W.WWWWW.\n"
            "WW.WWW..\n"
            "WWWWWW.B\n"
            "WWWBWBBB\n"
            "WWWWBBBB\n"
            "W.W.BBBB\n"
            "...BBBBB\n"
            "...BBBBB\n"
            "side=B",
            "fixed-seed 14-empty fixture with a root move that leaves the opponent with no legal "
            "move",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "14-empty-corner-choice", 14,
            "experimental_14,corner_choice,corner_available,high_mobility,edge_heavy",
            ".BW.W...\n"
            "WWWWWWWB\n"
            "W.WBBBBB\n"
            "WWBWWBB.\n"
            "W.BWBWWB\n"
            ".WWBBBW.\n"
            "WWWWWWWW\n"
            ".BB..WB.\n"
            "side=B",
            "fixed-seed 14-empty fixture with four legal corner moves", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "14-empty-corner-race", 14,
            "experimental_14,corner_race,corner_available,edge_heavy,high_mobility",
            "BW.WB.B.\n"
            "WWWWWWWW\n"
            "WWWWBWB.\n"
            ".WWBBBBB\n"
            "WWWBBBBW\n"
            ".WBWBBW.\n"
            "WWWWWWW.\n"
            "..W...W.\n"
            "side=B",
            "fixed-seed 14-empty edge-heavy fixture with competing corner access", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "14-empty-x-square-risk", 14,
                              "experimental_14,x_square_risk,normal_mobility,edge_heavy",
                              "..BBBW.W\n"
                              "...BW..W\n"
                              "WWWWBBWW\n"
                              ".W.BBWBB\n"
                              "B.BBWWBB\n"
                              "BBWWWBWB\n"
                              "B.B.WW.B\n"
                              "BBBBBBBB\n"
                              "side=B",
                              "fixed-seed 14-empty fixture with legal X-square moves", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "14-empty-edge-heavy", 14,
            "experimental_14,edge_heavy,corner_available,high_mobility,balanced_count",
            "W...BB..\n"
            "WWWWBWW.\n"
            "WWWBBWW.\n"
            "WWWBBBBW\n"
            "WWBWBBBB\n"
            ".WBBBWB.\n"
            ".WWBBBBW\n"
            "...WBBB.\n"
            "side=B",
            "fixed-seed 14-empty fixture with fourteen empty edge squares", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "14-empty-balanced-count", 14,
                              "experimental_14,balanced_count,edge_heavy,low_mobility",
                              "WWW.B...\n"
                              "..BBBB..\n"
                              "..BBBB..\n"
                              "BBBBW.BW\n"
                              ".BWWWBBW\n"
                              "BWBBBBWW\n"
                              "WWWWWWWW\n"
                              "WBBBWWWW\n"
                              "side=B",
                              "fixed-seed 14-empty fixture with equal disc counts", false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "14-empty-score-lopsided", 14,
                              "experimental_14,score_lopsided,edge_heavy,low_mobility",
                              "WWWWWWW.\n"
                              "WWWWWW..\n"
                              "WWWWW...\n"
                              "WWWWW...\n"
                              "WWWWW...\n"
                              "WWWWWW.B\n"
                              "WWWWWWB.\n"
                              "WWWWWBBB\n"
                              "side=W",
                              "fixed-seed 14-empty fixture with a large disc-count imbalance",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "14-empty-parity-ish", 14,
                              "experimental_14,parity-ish,edge_heavy,high_mobility,balanced_count",
                              "WW.WB..B\n"
                              "BWWBWBWW\n"
                              ".WBBWWW.\n"
                              "BBBBBWWB\n"
                              "BBWWWWWW\n"
                              ".WBBBBW.\n"
                              "WWBBBWB.\n"
                              "...B..B.\n"
                              "side=B",
                              "fixed-seed balanced 14-empty fixture with many edge-region empties",
                              false)) {
        return std::nullopt;
    }

    return positions;
}

} // namespace othello::benchmarks
