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
    positions.reserve(14);

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

    return positions;
}

} // namespace othello::benchmarks
