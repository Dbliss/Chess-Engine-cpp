// src/book_builder.cpp
#include "chess.h"
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <iostream>

struct TempMove {
    Move move;
    uint32_t count;
};

static bool isResultToken(const std::string& tok) {
    return tok == "1-0" || tok == "0-1" || tok == "1/2-1/2";
}

int main() {
    std::vector<std::string> files = {
        "Magnus.uci",
        "Nakamura.uci"
    };

    std::unordered_map<uint64_t, std::vector<TempMove>> book;

    for (const auto& file : files) {
        std::ifstream in(file);
        if (!in) {
            std::cerr << "Failed to open " << file << "\n";
            continue;
        }

        Board board;   // start position
        int ply = 0;

        std::string tok;
        while (in >> tok) {

            // End of game → reset
            if (isResultToken(tok)) {
                board = Board();
                ply = 0;
                continue;
            }

            // Only first 15 moves (30 plies)
            if (ply >= 30) {
                board = Board();
                ply = 0;
                continue;
            }

            uint64_t key = board.zobristHash;

            // Convert UCI move → Move
            Move mv = convertToMoveObject(tok);
            if (mv.from < 0 || mv.to < 0) {
                std::cerr << "Invalid UCI move: " << tok << "\n";
                board = Board();
                ply = 0;
                continue;
            }

            // Record weighted move
            auto& vec = book[key];
            bool found = false;
            for (auto& e : vec) {
                if (e.move == mv) {
                    e.count++;
                    found = true;
                    break;
                }
            }
            if (!found) {
                vec.push_back({ mv, 1 });
            }

            Undo u;
            board.makeMove(mv, u);
            ply++;
        }
    }

    // Write binary opening book
    std::ofstream out("opening_book.bin", std::ios::binary);
    if (!out) {
        std::cerr << "Failed to write opening_book.bin\n";
        return 1;
    }

    // NOTE: key in unordered_map is const, so write() must take const char*
    for (const auto& kv : book) {
        const uint64_t key = kv.first;
        const auto& moves = kv.second;

        uint16_t count = static_cast<uint16_t>(moves.size());

        out.write(reinterpret_cast<const char*>(&key), sizeof(key));
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));

        for (const auto& m : moves) {
            uint16_t from   = static_cast<uint16_t>(m.move.from);
            uint16_t to     = static_cast<uint16_t>(m.move.to);
            uint16_t promo  = static_cast<uint16_t>(m.move.promotion);
            uint32_t weight = m.count;

            out.write(reinterpret_cast<const char*>(&from), sizeof(from));
            out.write(reinterpret_cast<const char*>(&to), sizeof(to));
            out.write(reinterpret_cast<const char*>(&promo), sizeof(promo));
            out.write(reinterpret_cast<const char*>(&weight), sizeof(weight));
        }
    }

    std::cout << "Opening book built: " << book.size() << " positions\n";
    return 0;
}
