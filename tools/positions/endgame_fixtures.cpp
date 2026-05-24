#include "positions/endgame_fixtures.hpp"

#include <iostream>

namespace othello::benchmarks {

[[nodiscard]] bool add_endgame_position(std::vector<EndgamePosition>& positions,
                                               std::string_view name, int empties,
                                               std::string_view tags, std::string_view board_text,
                                               std::string_view notes, bool smoke) {
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

[[nodiscard]] std::optional<std::vector<EndgamePosition>> make_endgame_positions() {
    std::vector<EndgamePosition> positions;
    positions.reserve(66);

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

    if (!add_endgame_position(positions, "one-empty-terminal-no-move", 1,
                              "last_n,low_mobility,rule_core_regression",
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBBB\n"
                              "BBBBBBB.\n"
                              "side=B",
                              "hand-crafted one-empty terminal shape with no legal move for "
                              "either side",
                              false)) {
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

    if (!add_endgame_position(
            positions, "two-empty-tie-break-opponent-pass", 2,
            "last_n,tie_break,opponent_pass_after_move,edge_heavy,rule_core_regression",
            "WBBBBBBB\n"
            "WWBWWWWB\n"
            "WWWBWBWW\n"
            "WWWWWBWW\n"
            "WWWBWBW.\n"
            "WWBWBWWW\n"
            "WWWWBBWW\n"
            "BW.WBBWW\n"
            "side=B",
            "fixed-seed legal playout, seed 1 policy 0; equal best margins and a root move can "
            "force an opponent pass",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "two-empty-corner-pass-after-move", 2,
            "last_n,corner_choice,opponent_pass_after_move,edge_heavy,rule_core_regression",
            "BWWWWWWW\n"
            "BWWBWWWW\n"
            "BWWWWWWW\n"
            ".WWWBBWB\n"
            "WWWWWWBB\n"
            "WWBBBBBB\n"
            "WWWWBWBB\n"
            ".WWWWWWB\n"
            "side=B",
            "fixed-seed legal playout, seed 4 policy 0; legal corner choice includes a move that "
            "leaves the opponent without a legal reply",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "three-empty-diagonal-corner", 3,
            "last_n,diagonal,corner_choice,x_square_risk,tie_break,rule_core_regression",
            "B.WWWWBW\n"
            "BWWWWBBB\n"
            "BWWBBWBW\n"
            "BBBBWBWW\n"
            "WBWWBWWW\n"
            "WBWWWBWW\n"
            "WWWWWW.W\n"
            "WWWBBBB.\n"
            "side=W",
            "fixed-seed legal playout, seed 2 policy 0; singleton empties exercise diagonal and "
            "corner-adjacent last-N play",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "three-empty-opponent-pass-after-move", 3,
            "last_n,opponent_pass_after_move,edge_wrap,tie_break,rule_core_regression",
            "WWWWWWWW\n"
            ".WWWWWWW\n"
            "BBBBBBWW\n"
            "WBBWWBWW\n"
            "WWBBWWWW\n"
            "WBWBBWWW\n"
            "WWBW.WWW\n"
            "WB.BWWWB\n"
            "side=W",
            "fixed-seed legal playout, seed 6 policy 0; a root move can force an opponent pass "
            "with three empties remaining",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "three-empty-edge-wrap", 3,
                              "last_n,edge_wrap,edge_heavy,rule_core_regression",
                              "BBB.WWWW\n"
                              "BBBBWBWW\n"
                              "BBBWBWWW\n"
                              "BBBWBWWW\n"
                              "BBBWWWWW\n"
                              "BBBWWBWB\n"
                              ".BBBBWWB\n"
                              "BBBW.BWB\n"
                              "side=W",
                              "fixed-seed legal playout, seed 11 policy 0; all legal root moves "
                              "are edge moves near wrap-prone files",
                              false)) {
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

    if (!add_endgame_position(positions, "four-empty-parity-ish", 4,
                              "last_n,parity-ish,tie_break,edge_heavy,rule_core_regression",
                              "BBWWWWBB\n"
                              "WWWWWWBB\n"
                              "WWWWBWBW\n"
                              "BBWBBWBW\n"
                              "BWWBBWBW\n"
                              "WBBBB.WW\n"
                              ".WBBWWWW\n"
                              "WWWBB.B.\n"
                              "side=B",
                              "fixed-seed legal playout, seed 14 policy 0; four singleton empty "
                              "regions with equal best root margins",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "four-empty-root-pass-chain", 4,
                              "last_n,pass,pass_chain,edge_heavy,rule_core_regression",
                              "W.W..BWW\n"
                              "BBBBBBWW\n"
                              ".BWBWWWW\n"
                              "BBWWBWBW\n"
                              "BBWBWBBW\n"
                              "WBWBWBBW\n"
                              "WBBWWWBB\n"
                              "WBBBWWBB\n"
                              "side=B",
                              "fixed-seed legal playout, seed 27 policy 0; root side must pass "
                              "before the final four-empty sequence",
                              false)) {
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

    if (!add_endgame_position(positions, "six-empty-high-mobility-edge", 6,
                              "high_mobility,edge_heavy,corner_choice,rule_core_regression",
                              "WBBBBBBB\n"
                              "WWBBWBBB\n"
                              "WWWBBBBB\n"
                              "WWBBBBBB\n"
                              "WBWBWBBB\n"
                              "WWBWBWBB\n"
                              "WWWWWBWB\n"
                              "......BW\n"
                              "side=B",
                              "fixed-seed legal playout, seed 6 policy 3; six edge empties and "
                              "six legal root moves",
                              false)) {
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

    if (!add_endgame_position(positions, "eight-empty-root-pass", 8,
                              "pass,edge_heavy,low_mobility,rule_core_regression",
                              "WBBBBBBB\n"
                              "WWBBBBBB\n"
                              "WWBBBBBB\n"
                              "WBBBBBBB\n"
                              "WBWBWBBB\n"
                              "WWBWWBBB\n"
                              "WBBBBBBB\n"
                              "........\n"
                              "side=B",
                              "fixed-seed legal playout, seed 14 policy 3; root side must pass "
                              "with an eight-square edge region",
                              false)) {
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
            positions, "ten-empty-opponent-pass-after-move", 10,
            "opponent_pass_after_move,high_mobility,corner_choice,edge_heavy,rule_core_"
            "regression",
            "WWWWW...\n"
            ".WBBWWW.\n"
            "WWWWWWWB\n"
            "WWBBBWWW\n"
            "WBBWWBW.\n"
            "WWWWWWWW\n"
            "WWWWWWWB\n"
            "W..W.WW.\n"
            "side=B",
            "fixed-seed legal playout, seed 37 policy 0; ten legal root moves and a move that "
            "can force an opponent pass",
            false)) {
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
            positions, "twelve-empty-corner-parity", 12,
            "corner_choice,corner_available,parity-ish,edge_heavy,x_square_risk,rule_core_"
            "regression",
            ".W.W.BW.\n"
            "..WWBBBB\n"
            "WWWWBBB.\n"
            ".WWBBBBB\n"
            "BWBWBWBW\n"
            ".BWBWBWW\n"
            "BWBWWW.W\n"
            ".WWWWWW.\n"
            "side=B",
            "fixed-seed legal playout, seed 7 policy 1; many singleton odd regions with "
            "multiple corner candidates",
            false)) {
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

    if (!add_endgame_position(positions, "16-empty-low-mobility", 16,
                              "experimental_16,low_mobility,edge_heavy",
                              "...W.B.W\n"
                              "....BBWB\n"
                              "WB.BBWBB\n"
                              ".BBBWWBB\n"
                              ".BWBWBBB\n"
                              ".BWWWBBB\n"
                              ".BWWW.BB\n"
                              "BBBBW.WB\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 41 policy 0, three legal "
                              "root moves",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "16-empty-normal-mobility", 16,
                              "experimental_16,normal_mobility,edge_heavy,balanced_count",
                              "BBB..W.B\n"
                              "BWWB..B.\n"
                              ".W.WWBWW\n"
                              "WWBWBBWW\n"
                              "..WWWBWW\n"
                              ".BBWWWWW\n"
                              "BBBBBBBB\n"
                              "..B.W.W.\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 10 policy 0, six legal "
                              "root moves",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "16-empty-high-mobility", 16,
            "experimental_16,high_mobility,corner_available,corner_choice,edge_heavy,balanced_"
            "count",
            "...W.W..\n"
            "..WWW.WW\n"
            ".WWWBWW.\n"
            ".WBBBBB.\n"
            "WBWBBBBW\n"
            "BBBBBWWW\n"
            "BBBBWWWW\n"
            "BW.B.WW.\n"
            "side=B",
            "fixed-seed legal playout fixture, seed 50 policy 0, fourteen legal root moves",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "16-empty-root-pass", 16,
                              "experimental_16,pass,edge_heavy,score_lopsided",
                              "........\n"
                              "BBBBBBBW\n"
                              "BBWWBBB.\n"
                              "BBBBBBBB\n"
                              ".BBBBWBB\n"
                              "..BBBWBB\n"
                              "..BBBWWW\n"
                              "..BBBBBW\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 3398 policy 0, root side "
                              "must pass",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "16-empty-corner-choice", 16,
                              "experimental_16,corner_choice,corner_available,edge_heavy",
                              ".WB.B.BW\n"
                              ".WWWWWWW\n"
                              ".WBWWWWB\n"
                              "W.WBWWW.\n"
                              "WWBBBW.W\n"
                              "WBBBB.W.\n"
                              "WBBBBB..\n"
                              "WBW...B.\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 2 policy 0, open corner "
                              "choice",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "16-empty-corner-race", 16,
                              "experimental_16,corner_race,corner_available,edge_heavy",
                              ".BBBB...\n"
                              "..WWWB..\n"
                              ".WWWWW..\n"
                              "WWWBWWWW\n"
                              "WWWBWWWW\n"
                              "W.BWWWWW\n"
                              "WBBBWWW.\n"
                              "B.WWWW..\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 3 policy 0, competing "
                              "corner access",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "16-empty-edge-heavy", 16,
                              "experimental_16,edge_heavy,corner_available,high_mobility",
                              ".B....BW\n"
                              ".WWWWWW.\n"
                              ".WWWWBWW\n"
                              ".WBWWWWW\n"
                              "WWBWWBWW\n"
                              ".WWWBBWW\n"
                              "WWBBBBW.\n"
                              "W...B.W.\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 5043 policy 0, all empties "
                              "on edges",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "16-empty-parity-ish", 16,
                              "experimental_16,parity-ish,balanced_count,low_mobility",
                              "...W....\n"
                              "....W..B\n"
                              "BBBBBWBB\n"
                              ".BBBBBWB\n"
                              "BWBWBWBB\n"
                              "WWWBWBB.\n"
                              "WWWWWWB.\n"
                              "WWWWWWWB\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 5042 policy 0, balanced "
                              "disc count and four legal root moves",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "18-empty-low-mobility", 18,
                              "experimental_18,low_mobility,edge_heavy",
                              ".B...W..\n"
                              "B.B.BBBB\n"
                              "BBWBBWBB\n"
                              "BWBWBWWB\n"
                              ".BWBWW.W\n"
                              ".BBWBBB.\n"
                              "...WBBB.\n"
                              "..WWWWWW\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 19 policy 0, four legal "
                              "root moves",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "18-empty-normal-mobility", 18,
                              "experimental_18,normal_mobility,corner_available,edge_heavy",
                              ".B.W.BB.\n"
                              "..BBBBBB\n"
                              "B..WBWB.\n"
                              "BBBWWWW.\n"
                              "BBBWBWWB\n"
                              "..BBWBW.\n"
                              ".WBBBBWW\n"
                              "W...BBW.\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 4 policy 0, six legal "
                              "root moves",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "18-empty-high-mobility", 18,
                              "experimental_18,high_mobility,corner_available,edge_heavy",
                              "...W....\n"
                              ".WBWWW..\n"
                              "BWBBWWWB\n"
                              "WWBBBBWB\n"
                              "BWBBWWWW\n"
                              ".BWWBWWB\n"
                              "..WWWWW.\n"
                              "WWW...W.\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 47 policy 0, sixteen legal "
                              "root moves",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "18-empty-root-pass", 18,
                              "experimental_18,pass,edge_heavy,score_lopsided",
                              "..BBBBB.\n"
                              "....BB.W\n"
                              "....BWWW\n"
                              "W..B.WWW\n"
                              "WBBWWWWW\n"
                              "WB.BWB.W\n"
                              "W.BBWWWW\n"
                              "WWWWWWWW\n"
                              "side=B",
                              "reopened variant of the fixed-seed 14-empty pass fixture; root side "
                              "must pass",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "18-empty-corner-race", 18,
                              "experimental_18,corner_race,corner_available,edge_heavy",
                              ".B...WWW\n"
                              ".WBWWWBW\n"
                              "..BBWBWB\n"
                              ".WWWWWBB\n"
                              ".WWWWBBB\n"
                              "WWBWB..B\n"
                              "WWWBWW..\n"
                              ".W.BB...\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 34 policy 0, competing "
                              "corner access",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "18-empty-edge-heavy", 18,
            "experimental_18,edge_heavy,corner_available,high_mobility,rule_core_regression",
            "BW..WB..\n"
            "BWBBBBB.\n"
            ".WBBWWW.\n"
            "BWBBWW.W\n"
            ".WBWWWW.\n"
            ".WWWBWB.\n"
            ".WWWWB.B\n"
            "...BBBB.\n"
            "side=B",
            "fixed-seed legal playout, seed 4 policy 1; sixteen edge empties and thirteen legal "
            "root moves",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "18-empty-parity-ish", 18,
            "experimental_18,parity-ish,edge_heavy,high_mobility,rule_core_regression",
            "W.WB....\n"
            ".W.BBBBB\n"
            "W.WBW.B.\n"
            "WWBWBWBW\n"
            "WWWBWBW.\n"
            "WWWWWWB.\n"
            "WWWWWW.B\n"
            "...W.BW.\n"
            "side=B",
            "fixed-seed legal playout, seed 1 policy 0; twelve empty regions with ten odd "
            "regions",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "18-empty-corner-choice", 18,
            "experimental_18,corner_choice,corner_available,high_mobility,edge_heavy,rule_core_"
            "regression",
            ".W.W.BW.\n"
            "..WW.BW.\n"
            "W.BWWBW.\n"
            ".WBBWWBB\n"
            "BWBWBWBW\n"
            ".BWBWWWW\n"
            "BWWWWW.W\n"
            ".WW..B..\n"
            "side=B",
            "fixed-seed legal playout, seed 7 policy 1; fifteen legal root moves with three "
            "legal corners",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "18-empty-high-mobility-lite", 18,
            "experimental_18,high_mobility,corner_available,edge_heavy,x_square_risk,rule_core_"
            "regression",
            "BW..B...\n"
            ".W.BBWW.\n"
            ".WBBBWWB\n"
            ".BWBWBW.\n"
            "BWBWWBBW\n"
            "W.BWWBBB\n"
            "..WBWWBB\n"
            ".WB.B.B.\n"
            "side=B",
            "fixed-seed legal playout, seed 2 policy 1; fifteen legal root moves with broad edge "
            "pressure",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "18-empty-opponent-pass-after-move", 18,
            "experimental_18,opponent_pass_after_move,edge_heavy,high_mobility,rule_core_"
            "regression",
            ".W.....B\n"
            "BWWWW.B.\n"
            "BW.WWW..\n"
            "BWWWW.W.\n"
            "BWBBWWWB\n"
            ".WWBWWW.\n"
            ".WBWWWWW\n"
            "..BBBBBB\n"
            "side=B",
            "fixed-seed legal playout, seed 300 policy 1; a root move can force an opponent pass "
            "with eighteen empties",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-low-mobility", 20,
                              "experimental_20,low_mobility,low_branching",
                              "B......W\n"
                              "B.....W.\n"
                              "BWWWWW..\n"
                              "BWWWWWW.\n"
                              "BBWWW...\n"
                              "BBBBWW..\n"
                              "BBBBBBBB\n"
                              "BBBBBBBW\n"
                              "side=W",
                              "fixed-seed legal playout fixture, seed 28 policy 1, one legal "
                              "root move",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-root-pass", 20,
                              "experimental_20,pass,edge_heavy,low_branching",
                              "........\n"
                              "W....B..\n"
                              "WWW.BB..\n"
                              "WWWWBB..\n"
                              "WWWWWBBB\n"
                              "WWWWWWBB\n"
                              "WWWWWWW.\n"
                              "WWWWWWWW\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 266 policy 1, root side "
                              "must pass",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-edge-heavy-low-branching", 20,
                              "experimental_20,edge_heavy,low_branching",
                              "W.B.W.W.\n"
                              ".WBWWBB.\n"
                              "..W.W...\n"
                              "WWWWBBB.\n"
                              "WWWWBBBB\n"
                              "WBBWWB..\n"
                              "W.WWWW..\n"
                              "WWWWWW..\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 8 policy 1, three legal "
                              "root moves and many edge empties",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-corner-available-low-branching", 20,
                              "experimental_20,corner_available,low_branching",
                              ".....W..\n"
                              "......W.\n"
                              "B....BBW\n"
                              "BB.BBBBW\n"
                              "BBBBBWBB\n"
                              "BBWBWWB.\n"
                              "BBBBBBBB\n"
                              "BBBBBBBW\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 65 policy 1, three legal "
                              "root moves with one legal corner",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-normal-mobility", 20,
                              "experimental_20,mixed_20,normal_mobility,edge_heavy",
                              "B.WW...B\n"
                              "BBWBB.B.\n"
                              "BWWB.B..\n"
                              "BBWWB...\n"
                              "BB.BWW..\n"
                              "BBBWWW.W\n"
                              "B.BWWWWW\n"
                              "B..WWW.W\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 2 policy 0, five legal "
                              "root moves and mixed edge/interior empties",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(
            positions, "20-empty-high-mobility-lite", 20,
            "experimental_20,stress_lite_20,high_mobility,corner_available,edge_heavy,x_square_"
            "risk",
            "WWW.BBB.\n"
            ".B.WWWWW\n"
            "BBWBBBBW\n"
            ".WWWWBW.\n"
            ".BBWBWWW\n"
            "..BB.WWW\n"
            "..BBBW.W\n"
            "..B.....\n"
            "side=B",
            "fixed-seed legal playout fixture, seed 6 policy 0, nine legal root moves with "
            "corner access",
            false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-corner-race-lite", 20,
                              "experimental_20,stress_lite_20,corner_race,corner_available,"
                              "edge_heavy",
                              "....B...\n"
                              "..B.B.BW\n"
                              ".WB.BBB.\n"
                              "WWWWBBBB\n"
                              "WWWWWBBW\n"
                              "WWBWWBWW\n"
                              ".WB.BWW.\n"
                              ".WBB.WW.\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 7 policy 0, six legal root "
                              "moves with competing corner access",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-edge-heavy-stress-lite", 20,
                              "experimental_20,stress_lite_20,edge_heavy,normal_mobility",
                              "..W.....\n"
                              "WBWBBB..\n"
                              ".WWWBB..\n"
                              ".WWBBB..\n"
                              ".WWBWB.B\n"
                              "WBBWBBBB\n"
                              ".BWWWBWB\n"
                              "B.BBB.BB\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 3 policy 0, six legal root "
                              "moves and sixteen edge empties",
                              false)) {
        return std::nullopt;
    }

    if (!add_endgame_position(positions, "20-empty-parity-ish", 20,
                              "experimental_20,mixed_20,parity-ish,opponent_pass_after_move,"
                              "low_mobility",
                              "..W.B.W.\n"
                              "...W.WWW\n"
                              "WWWWWWW.\n"
                              "BW.WWW.B\n"
                              "WBWWWWBB\n"
                              "WWWWW..B\n"
                              "WWWWW...\n"
                              "WWWWW...\n"
                              "side=B",
                              "fixed-seed legal playout fixture, seed 10044 policy 1, many odd "
                              "regions and a move that can force an opponent pass",
                              false)) {
        return std::nullopt;
    }

    return positions;
}

} // namespace othello::benchmarks
