// ========================= engine.h =========================
#pragma once

#include "chess.h"   // Board, Move, Undo, TTFlag, isGoodCapture, getPieceValue, isNullViable, etc.
#include <cstdint>
#include <vector>
#include <chrono>
#include <utility>

// Keep this symbol available because your existing main.cpp calls it.
bool isEndgameDraw(int numWhiteBishops, int numWhiteKnights, int numBlackKnights, int numBlackBishops);

struct EngineConfig {
    // time control
    int drawPenalty = 30;
    int timeLimitMs = 100; // per-move

    bool useOpeningBook = true;

    // strength toggles (A/B testable)
    uint32_t timeCheckMask = 2047;  // check at (nodes & mask)==0 => ~every 2048 nodes

    // tuning safeguards
    int maxExtensionsPerLine = 3;
    int maxDepth = 100;

    // TT sizing
    uint64_t ttSizeMB = 64;

    // aspiration parameters
    int aspirationStartWindow = 100; // centipawns
    int aspirationGrowFactor = 2;   // window *= factor on fail-high/low

    // null move parameters
    int nullMoveReductionBase = 2;  // R = base + depth/3

    // (optional) eval tuning values
    int pawnValue   = 100;
    int knightValue = 325;
    int bishopValue = 325;
    int rookValue   = 500;
    int queenValue  = 975;

    // stop runaway games during tuning
    int maxGamePlies = 512;
};

struct EngineMovePicker {
    struct SM { Move m; int score; };

    Board& board;
    Move hashMove;
    bool hasHash;
    const Move killer1;
    const Move killer2;
    const bool useHistory;
    const int32_t (*history)[64];

    bool hashDone = false;

    Move killers[2];
    int killerCount = 0;
    int killerIdx = 0;

    SM goodCaps[256]; int goodN = 0; int goodIdx = 0;
    SM badCaps[256];  int badN  = 0; int badIdx  = 0;
    SM quiets[256];   int quietN= 0; int quietIdx= 0;

    EngineMovePicker(Board& b, const MoveList& moves, const Move& hm, const Move& k1, const Move& k2, bool useHist, const int32_t (*hist)[64])
        : board(b),
        hashMove(hm),
        hasHash(!(hm == NO_MOVE) && hm.from != -1),
        killer1(k1),
        killer2(k2),
        useHistory(useHist),
        history(hist)
    {
        bool foundHash = false;

        for (int i = 0; i < moves.size; ++i) {
            const Move& mv = moves.m[i];

            // Hash move: only if it exists in this position, and use the generated mv (correct isCapture)
            if (hasHash && mv == hashMove) {
                hashMove = mv;
                foundHash = true;
                continue;
            }

            // Captures/promos first
            if (mv.isCapture || mv.promotion) {
                int s = isGoodCapture(mv, board);
                if (mv.promotion) s += getPieceValue(mv.promotion) + 1000;

                if (s >= 0 || mv.promotion) goodCaps[goodN++] = { mv, s };
                else                        badCaps[badN++]  = { mv, s };
                continue;
            }

            // Killers: ONLY if present in generated moves, and store mv (correct flags)
            if (mv == killer1 || mv == killer2) {
                // avoid duplicates and avoid duplicating hash
                if ((!hasHash || !(mv == hashMove)) &&
                    (killerCount == 0 || !(mv == killers[0])) &&
                    killerCount < 2)
                {
                    killers[killerCount++] = mv;
                }
                continue;
            }

            // Quiets
            int s = 0;
            if (useHistory) {
                int idx = board.posToValue(mv.from);
                if (idx >= 0 && idx < 12) s = (int)history[idx][mv.to];
            }
            quiets[quietN++] = { mv, s };
        }

        // If TT move wasn't found among legal moves, don't try to play it.
        hasHash = hasHash && foundHash;
    }

    static inline bool pickBest(SM* arr, int n, int& idx, Move& out) {
        if (idx >= n) return false;
        int best = idx;
        int bestScore = arr[idx].score;
        for (int i = idx + 1; i < n; ++i) {
            if (arr[i].score > bestScore) { bestScore = arr[i].score; best = i; }
        }
        if (best != idx) std::swap(arr[best], arr[idx]);
        out = arr[idx].m;
        ++idx;
        return true;
    }

    bool next(Move& out) {
        if (!hashDone) {
            hashDone = true;
            if (hasHash) { out = hashMove; return true; }
        }
        if (pickBest(goodCaps, goodN, goodIdx, out)) return true;
        if (killerIdx < killerCount) { out = killers[killerIdx++]; return true; }
        if (pickBest(quiets, quietN, quietIdx, out)) return true;
        if (pickBest(badCaps, badN, badIdx, out)) return true;
        return false;
    }
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
    size_t transpositionSize() const;
    void printAfterMoveDebug(Engine& engine, Board& board);


private:
    // --- evaluation & search ---
    int evaluate(Board& board) const;

    int quiescence(Board& board, int alpha, int beta, int ply, bool& timedOut);
    int search(Board& board, int depth, int alpha, int beta, int startDepth, int ply, int totalExtensions, bool lastIterationNull, Move& bestMoveOut, bool& timedOut);


    // --- ordering & heuristics (engine-owned, not Board-owned) ---
    void orderMoves(Board& board, MoveList& moves, const Move& hashMove, int depth);
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
        uint16_t gen = 0;
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
    bool rootSideIsWhite_ = true;

    // heuristics
    static constexpr int MAX_PLY = 128;
    Move killers_[2][MAX_PLY]{};
    int32_t history_[12][64]{};
    int32_t maxHistoryValue_;

    // transposition table
    std::vector<EngineTTEntry> tt_;
    uint64_t ttMask_ = 0;
};
