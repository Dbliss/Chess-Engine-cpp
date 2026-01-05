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

struct EngineMovePicker {
    struct SM { Move m; int score; };

    Board& board;
    const Move hashMove;
    const bool hasHash;
    const Move killer1;
    const Move killer2;
    const bool useHistory;
    const uint64_t (*history)[64];

    bool hashDone = false;

    Move killers[2];
    int killerCount = 0;
    int killerIdx = 0;

    SM goodCaps[256]; int goodN = 0; int goodIdx = 0;
    SM badCaps[256];  int badN  = 0; int badIdx  = 0;
    SM quiets[256];   int quietN= 0; int quietIdx= 0;

    EngineMovePicker(Board& b,
                     const MoveList& moves,
                     const Move& hm,
                     const Move& k1,
                     const Move& k2,
                     bool useHist,
                     const uint64_t (*hist)[64])
        : board(b),
          hashMove(hm),
          hasHash(!(hm == Move(-1,-1)) && !(hm == NO_MOVE)),
          killer1(k1),
          killer2(k2),
          useHistory(useHist),
          history(hist)
    {
        // Collect killers (avoid dup and avoid hash move)
        if (!(killer1 == NO_MOVE) && killer1.from != -1 && (!hasHash || !(killer1 == hashMove))) {
            killers[killerCount++] = killer1;
        }
        if (!(killer2 == NO_MOVE) && killer2.from != -1 &&
            (!(killer2 == killer1)) && (!hasHash || !(killer2 == hashMove))) {
            killers[killerCount++] = killer2;
        }

        // Bucket moves once (no sort)
        for (int i = 0; i < moves.size; ++i) {
            const Move& mv = moves.m[i];
            if (hasHash && mv == hashMove) continue;

            if (mv.isCapture || mv.promotion) {
                int s = isGoodCapture(mv, board);
                if (mv.promotion) s += getPieceValue(mv.promotion) + 1000;

                if (s >= 0 || mv.promotion) goodCaps[goodN++] = { mv, s };
                else                        badCaps[badN++]  = { mv, s };
            } else if (mv == killer1 || mv == killer2) {
                // already queued in killers[]
                continue;
            } else {
                int s = 0;
                if (useHistory) {
                    int idx = board.posToValue(mv.from);
                    if (idx >= 0 && idx < 12) s = (int)history[idx][mv.to];
                }
                quiets[quietN++] = { mv, s };
            }
        }
    }

    static inline bool pickBest(SM* arr, int n, int& idx, Move& out) {
        if (idx >= n) return false;
        int best = idx;
        int bestScore = arr[idx].score;
        for (int i = idx + 1; i < n; ++i) {
            if (arr[i].score > bestScore) {
                bestScore = arr[i].score;
                best = i;
            }
        }
        if (best != idx) std::swap(arr[best], arr[idx]);
        out = arr[idx].m;
        ++idx;
        return true;
    }

    bool next(Move& out) {
        // stage 0: hash move
        if (!hashDone) {
            hashDone = true;
            if (hasHash) { out = hashMove; return true; }
        }

        // stage 1: good captures/promos
        if (pickBest(goodCaps, goodN, goodIdx, out)) return true;

        // stage 2: killers
        if (killerIdx < killerCount) {
            out = killers[killerIdx++];
            return true;
        }

        // stage 3: quiets by history
        if (pickBest(quiets, quietN, quietIdx, out)) return true;

        // stage 4: bad captures last
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
