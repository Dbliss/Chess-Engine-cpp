#include "chess.h"

#include <algorithm>
#include <cstdint>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#ifdef _WIN32
    #define NOMINMAX
    #include <windows.h>
#endif

// -----------------------------
// Console colour helpers
// -----------------------------
namespace ansi {
    static constexpr const char* reset = "\x1b[0m";
    static constexpr const char* grey  = "\x1b[90m";
    static constexpr const char* red   = "\x1b[31m";
    static constexpr const char* green = "\x1b[32m";

    static void enable_virtual_terminal() {
    #ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut == INVALID_HANDLE_VALUE) return;

        DWORD dwMode = 0;
        if (!GetConsoleMode(hOut, &dwMode)) return;

        dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, dwMode);
    #endif
    }

    static void clear_line() {
        std::cout << "\r\x1b[2K\r";
    }
}

// -----------------------------
// Perft counts (only what you want)
// -----------------------------
struct PerftCounts {
    uint64_t nodes    = 0;
    uint64_t captures = 0;
    uint64_t checks   = 0;
    uint64_t mates    = 0;
};

// Leaf checks/mates logic as discussed
static PerftCounts perft(Board& board, int depth) {
    PerftCounts out{};

    if (depth == 0) {
        out.nodes = 1;
        return out;
    }
    MoveList moves;
    board.generateAllMoves(moves);

    for (auto& m : moves) {
        Undo u;
        board.makeMove(m, u);

        if (depth == 1) {
            out.nodes += 1;
            if (m.isCapture) out.captures += 1;

            // Leaf: side-to-move is in check
            if (board.amIInCheck(board.whiteToMove)) {
                out.checks += 1;

                // Leaf mate: in check and no legal replies
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

// -----------------------------
// Move formatting using YOUR board mapping
// -----------------------------
//
// From your chess.cpp mapping:
//   fileChar = 'h' - file
//   rankChar = '1' + rank
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

// -----------------------------
// Stockfish-like divide ordering
// -----------------------------
static int pieceOrderSF(char pieceCharFromBoardGetPieceAt) {
    // Your getPieceAt returns:
    //   white pieces as lowercase: 'p','n','b','r','q','k'
    //   black pieces as uppercase: 'P','N','B','R','Q','K'
    // We'll normalize to lowercase and apply Stockfish-ish piece ordering.
    char p = pieceCharFromBoardGetPieceAt;
    if (p >= 'A' && p <= 'Z') p = char(p - 'A' + 'a');

    switch (p) {
        case 'p': return 0;
        case 'n': return 1;
        case 'b': return 2;
        case 'r': return 3;
        case 'q': return 4;
        case 'k': return 5;
        default:  return 9;
    }
}

static int fileIndexFromCoordChar(char f) {
    // 'a'..'h' -> 0..7
    if (f < 'a' || f > 'h') return 99;
    return int(f - 'a');
}

static int rankIndexFromCoordChar(char r) {
    // '1'..'8' -> 0..7
    if (r < '1' || r > '8') return 99;
    return int(r - '1');
}

static std::tuple<int,int,int,int,int,int,int> stockfishLikeKey(const Board& rootBoard, const Move& m) {
    // piece + move-kind + from/to squares
    char pc = rootBoard.getPieceAt(m.from);
    int pOrder = pieceOrderSF(pc);

    const std::string from = indexToCoord(m.from);
    const std::string to   = indexToCoord(m.to);

    int ff = fileIndexFromCoordChar(from[0]);
    int fr = rankIndexFromCoordChar(from[1]);
    int tf = fileIndexFromCoordChar(to[0]);
    int tr = rankIndexFromCoordChar(to[1]);

    // Move-kind ordering:
    // - pawns: single pushes first, then double pushes, then captures, then promotions (roughly matches Stockfish startpos feel)
    // - others: quiet first, then captures
    int kind = 0;

    char p = pc;
    if (p >= 'A' && p <= 'Z') p = char(p - 'A' + 'a');

    if (p == 'p') {
        bool isPromo = (m.promotion != 0);
        bool isCap = m.isCapture;
        int dr = (tr - fr);
        int absDr = (dr < 0) ? -dr : dr;

        if (isPromo) kind = 3;
        else if (!isCap && absDr == 1) kind = 0;      // single push
        else if (!isCap && absDr == 2) kind = 1;      // double push
        else if (isCap) kind = 2;                     // captures (and ep)
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

    // Key tuple sorts naturally ascending
    return {pOrder, kind, ff, fr, tf, tr, promo};
}

// -----------------------------
// Perft divide (nodes per root move) with SF-like ordering
// -----------------------------
struct DivideLine {
    std::string uci;
    uint64_t nodes;
    std::tuple<int,int,int,int,int,int,int> key;
};

static std::vector<DivideLine> perftDivideNodesStockfishOrder(Board& board, int depth) {
    std::vector<DivideLine> out;
    MoveList moves;
    board.generateAllMoves(moves);
    out.reserve(moves.size);

    // IMPORTANT: we must compute ordering key from the ROOT position,
    // so do it before makeMove() mutates the board.
    for (auto& m : moves) {
        DivideLine line;
        line.uci = moveToUCI(m);
        line.key = stockfishLikeKey(board, m);
        Undo u;
        board.makeMove(m, u);

        if (depth <= 1) line.nodes = 1;
        else line.nodes = perft(board, depth - 1).nodes;

        board.undoMove(m, u);

        out.push_back(std::move(line));
    }

    std::sort(out.begin(), out.end(),
              [](const DivideLine& a, const DivideLine& b) {
                  if (a.key != b.key) return a.key < b.key;
                  // final deterministic tie-breaker: UCI string
                  return a.uci < b.uci;
              });

    return out;
}

// -----------------------------
// Test definitions
// -----------------------------
struct Expected {
    std::optional<uint64_t> nodes;
    std::optional<uint64_t> captures;
    std::optional<uint64_t> checks;
    std::optional<uint64_t> mates;
};

struct PerftDepthCase {
    int depth = 0;
    Expected exp{};
};

struct PerftPositionSuite {
    std::string name;
    std::string fen;
    std::vector<PerftDepthCase> cases;
};

static bool check_field(const char* label, uint64_t got, const std::optional<uint64_t>& exp, std::string& whyFail) {
    if (!exp.has_value()) return true;
    if (got != exp.value()) {
        whyFail += std::string(" ") + label + "(got " + std::to_string(got) + " exp " + std::to_string(exp.value()) + ")";
        return false;
    }
    return true;
}

static std::string fmt_ms(double ms) {
    if (ms < 1000.0) return std::to_string((int)ms) + " ms";
    double s = ms / 1000.0;
    if (s < 60.0) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.2f s", s);
        return buf;
    }
    double m = s / 60.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.2f min", m);
    return buf;
}

static void printDivideDebug(const PerftPositionSuite& suite, int depth) {
    Board b;
    b.createBoardFromFEN(suite.fen);

    std::cout << ansi::grey
              << "---- Perft divide (nodes per root move) for " << suite.name
              << " at depth " << depth << " ----"
              << ansi::reset << "\n";

    auto lines = perftDivideNodesStockfishOrder(b, depth);

    uint64_t total = 0;
    for (const auto& ln : lines) {
        total += ln.nodes;
        std::cout << "  " << ln.uci << ": " << ln.nodes << "\n";
    }
    std::cout << ansi::grey << "  TOTAL: " << total << ansi::reset << "\n";
}

static bool run_one_depth(const PerftPositionSuite& suite, const PerftDepthCase& dc) {
    Board b;
    b.createBoardFromFEN(suite.fen);

    std::cout << ansi::grey
              << "[ " << suite.name << " | depth " << dc.depth << " ] "
              << "CALCULATING..."
              << ansi::reset
              << std::flush;

    const auto t0 = std::chrono::high_resolution_clock::now();
    PerftCounts got = perft(b, dc.depth);
    const auto t1 = std::chrono::high_resolution_clock::now();

    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    bool ok = true;
    std::string whyFail;

    ok &= check_field("Nodes",  got.nodes,    dc.exp.nodes,    whyFail);
    ok &= check_field("Caps",   got.captures, dc.exp.captures, whyFail);
    ok &= check_field("Checks", got.checks,   dc.exp.checks,   whyFail);
    ok &= check_field("Mates",  got.mates,    dc.exp.mates,    whyFail);

    ansi::clear_line();

    const bool hasAnyExpectation =
        dc.exp.nodes.has_value() || dc.exp.captures.has_value() || dc.exp.checks.has_value() || dc.exp.mates.has_value();

    const char* colour = ok ? ansi::green : ansi::red;
    const char* status = ok ? "PASS" : "FAIL";

    std::cout << colour
              << "[ " << suite.name << " | depth " << dc.depth << " ] "
              << status
              << ansi::reset
              << "  (" << fmt_ms(ms) << ")"
              << "  N:" << got.nodes
              << " C:" << got.captures
              << " K:" << got.checks
              << " M:" << got.mates;

    if (!hasAnyExpectation) {
        std::cout << "  " << ansi::grey << "(no expected values)" << ansi::reset;
    }

    if (!ok) {
        std::cout << "  " << ansi::red << whyFail << ansi::reset << "\n";
        printDivideDebug(suite, dc.depth);
    } else {
        std::cout << "\n";
    }

    return ok;
}

int main() {
    ansi::enable_virtual_terminal();

    const std::string pos1 = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
    const std::string pos2 = "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1";
    const std::string pos3 = "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1";
    const std::string pos4 = "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1";

    std::vector<PerftPositionSuite> suites = {
        {
            "Position 1",
            pos1,
            {
                {3, {8902ULL,      34ULL,      12ULL,      0ULL}},
                {4, {197281ULL,    1576ULL,    469ULL,     8ULL}},
                {5, {4865609ULL,   82719ULL,   27351ULL,   347ULL}},
                {6, {119060324ULL, 2812008ULL, 809099ULL,  10828ULL}},
            }
        },
        {
            "Position 2",
            pos2,
            {
                {1, {48ULL,        8ULL,       0ULL,       0ULL}},
                {2, {2039ULL,      351ULL,      3ULL,       0ULL}},
                {3, {97862ULL,     17102ULL,    993ULL,     1ULL}},
                {4, {4085603ULL,   757163ULL,   25523ULL,   43ULL}},
                {5, {193690690ULL, 35043416ULL, 3309887ULL, 30171ULL}},
            }
        },
        {
            "Position 3",
            pos3,
            {
                {1, {14ULL,      1ULL,     2ULL,     0ULL}},
                {2, {191ULL,     14ULL,    10ULL,    0ULL}},
                {3, {2812ULL,    209ULL,   267ULL,   0ULL}},
                {4, {43238ULL,   3348ULL,  1680ULL,  17ULL}},
                {5, {674624ULL,  52051ULL, 52950ULL, 0ULL}},
                {6, {11030083ULL,  940350ULL, 452473ULL, 2733ULL}},
                {7, {178633661ULL,  14519036ULL, 12797406ULL, 87ULL}},
                {8, {3009794393ULL,  267586558ULL, 135626805ULL, 450410ULL}},
            }
        },
        {
            "Position 4",
            pos4,
            {
                {1, {6ULL,        0ULL,      0ULL,      0ULL}},
                {2, {264ULL,      87ULL,     10ULL,     0ULL}},
                {3, {9467ULL,     1021ULL,   38ULL,     22ULL}},
                {4, {422333ULL,   131393ULL, 15492ULL,  5ULL}},
                {5, {15833292ULL, 2046173ULL,200568ULL, 50562ULL}},
            }
        }
    };

    int total = 0;
    int passed = 0;
    int skipped = 0;

    std::cout << ansi::grey
              << "Perft suite: Nodes / Captures / Checks / Mates\n"
              << "Rule: if a depth FAILs for a position, deeper depths for that position are skipped.\n"
              << "On FAIL: prints full perft-divide (Stockfish-like ordering) for that depth.\n"
              << ansi::reset;

    for (const auto& suite : suites) {
        std::cout << "\n" << ansi::grey << "=== " << suite.name << " ===" << ansi::reset << "\n";

        bool failedThisPos = false;
        for (const auto& dc : suite.cases) {
            if (failedThisPos) {
                skipped++;
                std::cout << ansi::grey
                          << "[ " << suite.name << " | depth " << dc.depth << " ] SKIPPED (previous depth failed)"
                          << ansi::reset << "\n";
                continue;
            }

            total++;
            bool ok = run_one_depth(suite, dc);
            if (ok) passed++;
            else failedThisPos = true;
        }
    }

    std::cout << "\nDone. Passed " << passed << " / " << total
              << " tests. Skipped " << skipped << ".\n";

    return (passed == total) ? 0 : 1;
}
