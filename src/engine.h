// ========================= engine.h =========================
#pragma once

#include "chess.h"   // Board, Move, Undo, TTFlag, isGoodCapture, getPieceValue, isNullViable, etc.
#include <cstdint>
#include <vector>
#include <chrono>

// Keep this symbol available because your existing main.cpp calls it.
bool isEndgameDraw(int numWhiteBishops, int numWhiteKnights, int numBlackKnights, int numBlackBishops);

struct EngineConfig {
    // time control
    int timeLimitMs = 300;          // per-move

    // structural switches (your “tuning knobs”)
    bool useOpeningBook = true;     // uses Board's loaded HASH_BOOK entries (transposition_table.dat)
    bool useTT = true;              // engine-owned TT (separate per engine instance)
    bool useNullMove = true;
    bool useLMR = true;
    bool useKillerMoves = true;
    bool useHistoryHeuristic = true;

    bool quiescenceIncludeChecks = false; // captures+promotions only vs +checks
    bool extendChecks = false;            // +1 ply extension if move gives check (capped)

    // tuning safeguards
    int maxExtensionsPerLine = 3;
    int maxDepth = 100;

    // TT sizing
    uint64_t ttSizeMB = 64;

    // (optional) eval tuning values
    int pawnValue   = 100;
    int knightValue = 325;
    int bishopValue = 325;
    int rookValue   = 500;
    int queenValue  = 975;

    // stop runaway games during tuning
    int maxGamePlies = 512;
};

class Engine {
public:
    explicit Engine(const EngineConfig& cfg = EngineConfig());

    // Call per move; returns chosen move (always legal if any legal moves exist).
    Move getMove(Board& board);

    // For fair engine-vs-engine: clears per-game state (TT, killers, history).
    void newGame();

    void setTimeLimitMs(int ms);
    int  getTimeLimitMs() const;

    EngineConfig& config() { return cfg_; }
    const EngineConfig& config() const { return cfg_; }

    int lastSearchNodes() const { return lastNodes_; }
    int lastSearchDepth() const { return lastDepth_; }
    int lastEval() const { return lastEval_; }

private:
    // --- evaluation & search ---
    int evaluate(Board& board) const;

    int quiescence(Board& board, int alpha, int beta, int ply, bool& timedOut);
    int search(Board& board,
               int depth,
               int alpha,
               int beta,
               int startDepth,
               int ply,
               int totalExtensions,
               bool lastIterationNull,
               Move& bestMoveOut,
               bool& timedOut);

    // --- ordering & heuristics (engine-owned, not Board-owned) ---
    std::vector<Move> orderMoves(Board& board, const std::vector<Move>& moves, const Move& hashMove, int depth);
    bool isKiller(const Move& m, int depth) const;
    void recordKiller(const Move& m, int depth);
    void updateHistory(Board& board, int from, int to, int bonus);

    // --- TT (engine-owned, per instance) ---
    struct EngineTTEntry {
        uint64_t key = 0;
        int score = 0;
        int depth = -1;
        TTFlag flag = HASH_FLAG_EXACT;
        Move move = Move(-1, -1);
    };

    void resizeTT(uint64_t mb);
    EngineTTEntry* probeTT(uint64_t key);
    void storeTT(uint64_t key, int score, TTFlag flag, const Move& move, int depth);

    // --- misc ---
    bool outOfTime() const;

private:
    EngineConfig cfg_;

    // timing / stats
    std::chrono::time_point<std::chrono::high_resolution_clock> endTime_{};
    int lastNodes_ = 0;
    int lastDepth_ = 0;
    int lastEval_  = 0;

    // heuristics
    static constexpr int MAX_PLY = 128;
    Move killers_[2][MAX_PLY]{};
    int32_t history_[12][64]{};
    int32_t maxHistoryValue_;

    // transposition table
    std::vector<EngineTTEntry> tt_;
    uint64_t ttMask_ = 0;
};
