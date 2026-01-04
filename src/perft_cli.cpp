#include "chess.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

// ------------------------------------------------------------
// Coordinate mapping matching YOUR engine (see chess.cpp):
// numToBoardPosition(): fileChar = 'h' - file, rankChar = '1' + rank
// ------------------------------------------------------------
static std::string indexToCoord(int sq) {
    if (sq < 0 || sq > 63) return "??";
    int rank = sq / 8;
    int file = sq % 8;
    char fileChar = char('h' - file);
    char rankChar = char('1' + rank);
    return std::string() + fileChar + rankChar;
}

static std::string moveToUCI(const Move& m) {
    std::string s = indexToCoord(m.from) + indexToCoord(m.to);
    if (m.promotion) {
        char p = m.promotion;
        if (p >= 'A' && p <= 'Z') p = char(p - 'A' + 'a');
        s.push_back(p);
    }
    return s;
}

static bool applyUCIMove(Board& board, const std::string& uci) {
    if (uci.size() < 4) return false;

    // Generate legal moves in the current position
    MoveList moves;
    board.generateAllMoves(moves);

    for (Move& m : moves) {
        if (moveToUCI(m) == uci) {
            Undo u;
            board.makeMove(m, u);
            return true;
        }
    }
    return false; // illegal move
}

// ------------------------------------------------------------
// Perft counts
// ------------------------------------------------------------
struct PerftCounts {
    uint64_t nodes    = 0;
    uint64_t captures = 0;
    uint64_t checks   = 0;
    uint64_t mates    = 0;
};

static PerftCounts perft(Board& board, int depth) {
    PerftCounts out{};
    if (depth == 0) { out.nodes = 1; return out; }

    MoveList moves;
    board.generateAllMoves(moves);

    for (auto& m : moves) {
        Undo u;
        board.makeMove(m, u);

        if (depth == 1) {
            out.nodes += 1;

            if (m.isCapture) out.captures += 1;

            // suites count check if side-to-move is in check at leaf
            if (board.amIInCheck(board.whiteToMove)) {
                out.checks += 1;
                MoveList tmp; 
                board.generateAllMoves(tmp);
                if (tmp.size == 0) {
                    out.mates += 1;
                }
            }
        } else {
            PerftCounts child = perft(board, depth - 1);
            out.nodes    += child.nodes;
            out.captures += child.captures;
            out.checks   += child.checks;
            out.mates    += child.mates;
        }
        board.undoMove(m, u);
    }

    return out;
}

// ------------------------------------------------------------
// Stockfish-like ordering for "divide" printing
// (pawn singles a->h, pawn doubles a->h, then knights, etc.)
// NOTE: "a->h" here is per your printed UCI coordinates.
// ------------------------------------------------------------
static int pieceOrderSF(char pc) {
    if (pc >= 'A' && pc <= 'Z') pc = char(pc - 'A' + 'a');
    switch (pc) {
        case 'p': return 0;
        case 'n': return 1;
        case 'b': return 2;
        case 'r': return 3;
        case 'q': return 4;
        case 'k': return 5;
        default:  return 9;
    }
}

static int fileIdx(char f) { return (f >= 'a' && f <= 'h') ? int(f - 'a') : 99; }
static int rankIdx(char r) { return (r >= '1' && r <= '8') ? int(r - '1') : 99; }

static std::tuple<int,int,int,int,int,int,int> stockfishLikeKey(const Board& root, const Move& m) {
    char pc = root.getPieceAt(m.from);
    int pOrder = pieceOrderSF(pc);

    std::string from = indexToCoord(m.from);
    std::string to   = indexToCoord(m.to);

    int ff = fileIdx(from[0]), fr = rankIdx(from[1]);
    int tf = fileIdx(to[0]),   tr = rankIdx(to[1]);

    char plc = pc;
    if (plc >= 'A' && plc <= 'Z') plc = char(plc - 'A' + 'a');

    int kind = 0;
    if (plc == 'p') {
        bool promo = (m.promotion != 0);
        bool cap   = m.isCapture;
        int dr = tr - fr;
        int absDr = dr < 0 ? -dr : dr;

        if (promo) kind = 3;
        else if (!cap && absDr == 1) kind = 0; // single
        else if (!cap && absDr == 2) kind = 1; // double
        else if (cap) kind = 2;
        else kind = 4;
    } else {
        kind = m.isCapture ? 1 : 0;
    }

    int promo = 0;
    if (m.promotion) {
        char pp = m.promotion;
        if (pp >= 'A' && pp <= 'Z') pp = char(pp - 'A' + 'a');
        promo = int(pp);
    }

    return {pOrder, kind, ff, fr, tf, tr, promo};
}

struct DivideLine {
    std::string uci;
    uint64_t nodes;
    std::tuple<int,int,int,int,int,int,int> key;
};

static std::vector<DivideLine> divide(Board& board, int depth) {
    std::vector<DivideLine> out;
    MoveList moves;
    board.generateAllMoves(moves);
    out.reserve(moves.size);

    for (auto& m : moves) {
        DivideLine ln;
        ln.uci = moveToUCI(m);
        ln.key = stockfishLikeKey(board, m);
        Undo u;
        board.makeMove(m, u);
        ln.nodes = (depth <= 1) ? 1 : perft(board, depth - 1).nodes;
        board.undoMove(m, u);

        out.push_back(std::move(ln));
    }

    std::sort(out.begin(), out.end(), [](const DivideLine& a, const DivideLine& board) {
        if (a.key != board.key) return a.key < board.key;
        return a.uci < board.uci;
    });

    return out;
}

// ------------------------------------------------------------
// Simple Stockfish-ish CLI
// ------------------------------------------------------------
static void print_help() {
    std::cout <<
        "Commands:\n"
        "  position startpos\n"
        "  position fen <FEN...>\n"
        "  perft <N>\n"
        "  divide <N>\n"
        "  d              (prints board + fen)\n"
        "  help\n"
        "  quit\n";
}

int main() {
    Board board; // creates start position by default
    std::string currentFEN = "startpos";

    std::cout << "ChessEngine perft CLI (Stockfish-style)\n";
    print_help();

    for (std::string line; std::cout << "\n> " && std::getline(std::cin, line); ) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;
        if (cmd.empty()) continue;

        if (cmd == "quit" || cmd == "exit") {
            break;
        }

        if (cmd == "help") {
            print_help();
            continue;
        }

        if (cmd == "d") {
            board.printBoard();
            continue;
        }

        if (cmd == "position") {
            std::string sub;
            iss >> sub;

            if (sub == "startpos") {
                board = Board();
                board.createBoard();
                currentFEN = "startpos";

                // Optional: parse "moves ..."
                std::string token;
                iss >> token;
                if (token == "moves") {
                    std::string mv;
                    while (iss >> mv) {
                        if (!applyUCIMove(board, mv)) {
                            std::cout << "Illegal move: " << mv << "\n";
                            break;
                        }
                    }
                }

                std::cout << "ok\n";
                continue;
            }

            if (sub == "fen") {
                std::string fenPart, fen;
                for (int i = 0; i < 6; i++) {
                    iss >> fenPart;
                    fen += fenPart + " ";
                }
                if (!fen.empty()) fen.pop_back();

                board = Board();
                board.createBoardFromFEN(fen);
                currentFEN = fen;

                // Optional: parse "moves ..."
                std::string token;
                iss >> token;
                if (token == "moves") {
                    std::string mv;
                    while (iss >> mv) {
                        if (!applyUCIMove(board, mv)) {
                            std::cout << "Illegal move: " << mv << "\n";
                            break;
                        }
                    }
                }

                std::cout << "ok\n";
                continue;
            }

            std::cout << "error: expected startpos or fen\n";
            continue;
        }

        if (cmd == "perft") {
            int depth = 0;
            iss >> depth;
            if (depth < 0) {
                std::cout << "error: depth must be >= 0\n";
                continue;
            }

            Board b = board; // run on a copy so CLI state stays unchanged
            auto t0 = std::chrono::high_resolution_clock::now();
            PerftCounts r = perft(b, depth);
            auto t1 = std::chrono::high_resolution_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            std::cout << "Nodes: " << r.nodes
                      << "  Captures: " << r.captures
                      << "  Checks: " << r.checks
                      << "  Mates: " << r.mates
                      << "  Time: " << ms << " ms\n";
            continue;
        }

        if (cmd == "divide") {
            int depth = 0;
            iss >> depth;
            if (depth <= 0) {
                std::cout << "error: depth must be >= 1\n";
                continue;
            }

            Board b = board; // copy
            auto t0 = std::chrono::high_resolution_clock::now();
            auto lines = divide(b, depth);
            auto t1 = std::chrono::high_resolution_clock::now();

            uint64_t total = 0;
            for (auto& ln : lines) {
                total += ln.nodes;
                std::cout << ln.uci << ": " << ln.nodes << "\n";
            }
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            std::cout << "Total: " << total << "  Time: " << ms << " ms\n";
            continue;
        }

        std::cout << "Unknown command. Type 'help'.\n";
    }

    return 0;
}
