// ========================= engine.cpp =========================
#include "engine.h"
#include "opening_book.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <bit>

static constexpr int MATE_SCORE = 20000;
static constexpr int MATE_THRESHOLD = 19000; // anything beyond this is treated as mate

static inline int scoreToTT(int score, int ply) {
    if (score >  MATE_THRESHOLD) return score + ply; // store as "mate score" independent of ply
    if (score < -MATE_THRESHOLD) return score - ply;
    return score;
}

static inline int scoreFromTT(int score, int ply) {
    if (score >  MATE_THRESHOLD) return score - ply; // restore "mate in N" at this ply
    if (score < -MATE_THRESHOLD) return score + ply;
    return score;
}

bool isEndgameDraw(int numWhiteBishops, int numWhiteKnights, int numBlackKnights, int numBlackBishops) {
    int totalWhite = numWhiteKnights + numWhiteBishops;
    int totalBlack = numBlackKnights + numBlackBishops;

    if (std::abs(totalWhite - totalBlack) >= 3) return false;
    if (numWhiteKnights == 1 && numWhiteBishops == 2 && totalBlack <= 1) return false;
    if (numBlackKnights == 1 && numBlackBishops == 2 && totalWhite <= 1) return false;
    if ((numWhiteBishops == 2 && totalBlack == 0) || (numBlackBishops == 2 && totalWhite == 0)) return false;
    return true;
}

static inline unsigned ctz64(uint64_t x) {
    return x ? (unsigned)std::countr_zero(x) : 64u;
}

static inline int kingDistance(uint64_t king1, uint64_t king2) {
    int i1 = (int)ctz64(king1);
    int i2 = (int)ctz64(king2);
    int x1 = i1 % 8;
    int y1 = i1 / 8;
    int x2 = i2 % 8;
    int y2 = i2 / 8;
    return std::max(std::abs(x1 - x2), std::abs(y1 - y2));
}

Engine::Engine(const EngineConfig& cfg)
    : cfg_(cfg) {
    resizeTT(cfg_.ttSizeMB);
    newGame();
}

void Engine::resizeTT(uint64_t mb) {
    // power-of-two entries, similar to your Board::resize_tt
    size_t entries = (size_t)((mb * 1048576ull) / sizeof(EngineTTEntry));
    if (entries < 1024) entries = 1024;

    size_t pow2 = 1;
    while ((pow2 << 1) <= entries) pow2 <<= 1;

    tt_.assign(pow2, EngineTTEntry{});
    ttMask_ = (uint64_t)(pow2 - 1);
}

Engine::EngineTTEntry* Engine::probeTT(uint64_t key) {
    if (tt_.empty()) return nullptr;
    return &tt_[key & ttMask_];
}

void Engine::storeTT(uint64_t key, int score, TTFlag flag, const Move& move, int depth) {
    if (tt_.empty()) return;
    EngineTTEntry& e = tt_[key & ttMask_];

    // replace if different key, deeper, or exact info
    if (e.key != key || depth > e.depth || flag == HASH_FLAG_EXACT) {
        e.key = key;
        e.score = score;
        e.flag = flag;
        e.move = move;
        e.depth = depth;
    }
}

void Engine::newGame() {
    // clear TT
    for (auto& e : tt_) e = EngineTTEntry{};

    // clear heuristics
    for (int i = 0; i < 2; ++i) {
        for (int d = 0; d < MAX_PLY; ++d) {
            killers_[i][d] = NO_MOVE;
        }
    }

    for (int p = 0; p < 12; ++p) {
        for (int sq = 0; sq < 64; ++sq) history_[p][sq] = 0;
    }
    maxHistoryValue_ = 1 << 16;
}

bool Engine::outOfTime() const {
    return std::chrono::high_resolution_clock::now() > endTime_;
}

bool Engine::isKiller(const Move& m, int depth) const {
    if (depth < 0 || depth >= MAX_PLY) return false;
    return (m == killers_[0][depth]) || (m == killers_[1][depth]);
}

void Engine::recordKiller(const Move& m, int depth) {
    if (depth < 0 || depth >= MAX_PLY) return;
    if (m.isCapture) return;

    if (!(m == killers_[0][depth])) {
        killers_[1][depth] = killers_[0][depth];
        killers_[0][depth] = m;
    }
}

void Engine::updateHistory(Board& board, int from, int to, int bonus) {
    if (to < 0 || to >= 64) return;

    int idx = board.posToValue(from);
    if (idx < 0 || idx >= 12) return;

    int64_t v = (int64_t)history_[idx][to] + (int64_t)bonus;
    if (v < 0) v = 0;
    if (v > maxHistoryValue_) v = maxHistoryValue_;
    history_[idx][to] = (int32_t)v;


    // updateHistory scaling behavior
    if (history_[idx][to] >= maxHistoryValue_) {
        maxHistoryValue_ <<= 1;
        for (int i = 0; i < 12; ++i) {
            for (int j = 0; j < 64; ++j) {
                history_[i][j] >>= 1;
            }
        }
    }
}

void Engine::orderMoves(Board& board, MoveList& moves, const Move& hashMove, int depth) {
    struct LocalScored {
        Move m;
        int  score; // signed
    };

    LocalScored scored[256];
    const int n = moves.size;

    for (int i = 0; i < n; ++i) {
        const Move& m = moves.m[i];
        int score = 0;

        if (m == hashMove) {
            score = (int)maxHistoryValue_ + 100;
        }
        else if (m.isCapture || m.promotion) {
            int cap = isGoodCapture(m, board);
            score += cap;

            if (m.promotion) score += getPieceValue(m.promotion) + 1000; // promo bias
            if (cap >= 0 || m.promotion) score += (int)maxHistoryValue_ + 1;
            // else: keep negative captures low
        }
        else if (isKiller(m, depth)) {
            score = (int)maxHistoryValue_;
        }
        else {
            int idx = board.posToValue(m.from);
            if (idx >= 0 && idx < 12) score = (int)history_[idx][m.to];
        }

        scored[i] = { m, score };
    }

    std::sort(scored, scored + n, [](const LocalScored& a, const LocalScored& b) {
        return a.score > b.score;
    });

    for (int i = 0; i < n; ++i) {
        moves.m[i] = scored[i].m;
    }
}

int Engine::evaluate(Board& board) const {
    // quick draw: kings only
    if ((std::popcount(board.whitePieces) == 1) && (std::popcount(board.blackPieces) == 1)) return 0;
    
    MoveList tmp;


    // piece-square tables from your engine2.cpp
    static const int pawn_pcsq[64] = {
          0,   0,   0,   0,   0,   0,   0,   0,
         15,  20,  30,  40,  40,  30,  20,  15,
         10,  10,  20,  30,  30,  20,  10,  10,
          5,   5,  10,  25,  25,  10,   5,   5,
          0,   0,   0,  20,  20,   0,   0,   0,
          5,  -5, -10, -30, -30, -10,  -5,   5,
          5,  10,  10, -20, -20,  10,  10,   5,
          0,   0,   0,   0,   0,   0,   0,   0
    };

    static const int knight_pcsq[64] = {
        -50, -40, -30, -30, -30, -30, -40, -50,
        -40, -20,   0,   0,   0,   0, -20, -40,
        -30,   0,  10,  15,  15,  10,   0, -30,
        -30,   5,  15,  20,  20,  15,   5, -30,
        -30,   0,  15,  20,  20,  15,   0, -30,
        -30,   5,  10,  15,  15,  10,   5, -30,
        -40, -20,   0,   5,   5,   0, -20, -40,
        -50, -40, -30, -30, -30, -30, -40, -50
    };

    static const int bishop_pcsq[64] = {
        -10, -10, -10, -10, -10, -10, -10, -10,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10,   0,   5,   5,   5,   5,   0, -10,
        -10,   0,   5,  10,  10,   5,   0, -10,
        -10,   0,   5,  10,  10,   5,   0, -10,
        -10,   0,   5,   5,   5,   5,   0, -10,
        -10,   0,   0,   0,   0,   0,   0, -10,
        -10, -10, -20, -10, -10, -20, -10, -10
    };

    static const int king_pcsq[64] = {
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -20, -20, -20, -20, -20, -20, -20, -20,
          0,  20,  40, -20,   0, -20,  40,  20
    };

    static const int king_pcsq_black[64] = {
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -40, -40, -40, -40, -40, -40, -40, -40,
        -20, -20, -20, -20, -20, -20, -20, -20,
         20,  40, -20,   0, -20,  40,  20,   0
    };

    static const int king_endgame_pcsq[64] = {
        -40, -30, -20, -10, -10, -20, -30, -40,
        -30, -10,   0,  10,  10,   0, -10, -30,
        -20,   0,  30,  50,  50,  30,   0, -20,
        -10,  10,  50,  60,  60,  50,  10, -10,
        -10,  10,  50,  60,  60,  50,  10, -10,
        -20,   0,  30,  50,  50,  30,   0, -20,
        -30, -10,   0,  10,  10,   0, -10, -30,
        -40, -30, -20, -10, -10, -20, -30, -40
    };

    auto posValWhite = [](uint64_t bb, const int pcsq[64]) {
        int v = 0;
        while (bb) {
            int idx = (int)ctz64(bb);
            v += pcsq[63 - idx];
            bb &= bb - 1;
        }
        return v;
    };

    auto posValBlack = [](uint64_t bb, const int pcsq[64]) {
        int v = 0;
        while (bb) {
            int idx = (int)ctz64(bb);
            v += pcsq[idx];
            bb &= bb - 1;
        }
        return v;
    };

    const int pawnValue   = cfg_.pawnValue;
    const int knightValue = cfg_.knightValue;
    const int bishopValue = cfg_.bishopValue;
    const int rookValue   = cfg_.rookValue;
    const int queenValue  = cfg_.queenValue;

    int numWhitePawns   = std::popcount(board.whitePawns);
    int numWhiteBishops = std::popcount(board.whiteBishops);
    int numWhiteKnights = std::popcount(board.whiteKnights);
    int numWhiteRooks   = std::popcount(board.whiteRooks);
    int numWhiteQueens  = std::popcount(board.whiteQueens);

    int numBlackPawns   = std::popcount(board.blackPawns);
    int numBlackBishops = std::popcount(board.blackBishops);
    int numBlackKnights = std::popcount(board.blackKnights);
    int numBlackRooks   = std::popcount(board.blackRooks);
    int numBlackQueens  = std::popcount(board.blackQueens);

    // “endgame draw encouragement” from your engine2
    if (!numWhitePawns && !numBlackPawns &&
        !numWhiteQueens && !numBlackQueens &&
        !numWhiteRooks && !numBlackRooks) {
        if (isEndgameDraw(numWhiteBishops, numWhiteKnights, numBlackKnights, numBlackBishops)) {
            return board.whiteToMove ? -5 : 5;
        }
    }

    const uint64_t fileMasks[8] = {
        0x0101010101010101ULL, 0x0202020202020202ULL, 0x0404040404040404ULL, 0x0808080808080808ULL,
        0x1010101010101010ULL, 0x2020202020202020ULL, 0x4040404040404040ULL, 0x8080808080808080ULL
    };

    const uint64_t rankMasks[13] = {
        0xFFULL, 0xFF00ULL, 0xFF0000ULL, 0xFF000000ULL,
        0xFF00000000ULL, 0xFF0000000000ULL, 0xFF000000000000ULL, 0xFF00000000000000ULL,
        0ULL, 0ULL, 0ULL, 0ULL, 0ULL
    };

    int pawnProgressBonus[8] = { 0, 10, 20, 30, 50, 70, 90, 0 };
    int passedPawnBonus[8]   = { 0, 10, 20, 30, 50, 70, 90, 0 };

    // material phase
    const double totalMaterial =
        16.0 * pawnValue + 4.0 * knightValue + 4.0 * bishopValue + 4.0 * rookValue + 2.0 * queenValue;

    double whiteMaterial =
        numWhitePawns * pawnValue +
        numWhiteKnights * knightValue +
        numWhiteBishops * bishopValue +
        numWhiteRooks * rookValue +
        numWhiteQueens * queenValue;

    double blackMaterial =
        numBlackPawns * pawnValue +
        numBlackKnights * knightValue +
        numBlackBishops * bishopValue +
        numBlackRooks * rookValue +
        numBlackQueens * queenValue;

    double currentMaterial = whiteMaterial + blackMaterial;
    double gamePhase = (totalMaterial - currentMaterial) / totalMaterial;

    double result = 0.0;

    // bishops stronger with fewer pawns
    int numPawns = numWhitePawns + numBlackPawns;
    double bishopMultiplier = 5.0 * (16 - numPawns);

    result += numWhiteBishops * bishopMultiplier;
    result -= numBlackBishops * bishopMultiplier;

    if (numWhiteBishops == 2) result += bishopMultiplier;
    if (numBlackBishops == 2) result -= bishopMultiplier;

    // no pawns late
    if (gamePhase > 0.6) {
        if (numWhitePawns < 1 && numWhiteQueens == 0) result -= 140 * gamePhase;
        if (numBlackPawns < 1 && numBlackQueens == 0) result += 140 * gamePhase;
    } else {
        // early king safety
        const uint64_t notHFile = 0xFEFEFEFEFEFEFEFEULL;
        const uint64_t notAFile = 0x7F7F7F7F7F7F7F7FULL;

        int kingSafetyBonus[6] = { -150, -50, -20, 0, 5, 10 };
        int pawnStormBonus[8]  = { 0, 0, 0, 5, 10, 12, 15, 0 };

        auto countShield = [&](uint64_t king, uint64_t pawns, bool isWhite) {
            uint64_t north = isWhite ? (king << 8) : (king >> 8);
            uint64_t nw    = isWhite ? ((king << 9) & notAFile) : ((king >> 9) & notHFile);
            uint64_t ne    = isWhite ? ((king << 7) & notHFile) : ((king >> 7) & notAFile);
            uint64_t west  = (king << 1) & notAFile;
            uint64_t east  = (king >> 1) & notHFile;
            uint64_t shield = (north | nw | ne | west | east) & pawns;
            int c = std::popcount(shield);
            if (c < 0) c = 0;
            if (c > 5) c = 5;
            return c;
        };

        int whiteDef = countShield(board.whiteKing, board.whitePawns, true);
        int blackDef = countShield(board.blackKing, board.blackPawns, false);
        result += kingSafetyBonus[whiteDef];
        result -= kingSafetyBonus[blackDef];

        int whiteKingFile = (int)(ctz64(board.whiteKing) % 8);
        int blackKingFile = (int)(ctz64(board.blackKing) % 8);

        uint64_t whiteFileMask = fileMasks[whiteKingFile];
        uint64_t blackFileMask = fileMasks[blackKingFile];

        if (whiteKingFile > 0) whiteFileMask |= fileMasks[whiteKingFile - 1];
        if (whiteKingFile < 7) whiteFileMask |= fileMasks[whiteKingFile + 1];
        if (blackKingFile > 0) blackFileMask |= fileMasks[blackKingFile - 1];
        if (blackKingFile < 7) blackFileMask |= fileMasks[blackKingFile + 1];

        for (int rank = 1; rank <= 6; ++rank) {
            uint64_t wp = board.whitePawns & rankMasks[rank] & whiteFileMask;
            uint64_t bp = board.blackPawns & rankMasks[7 - rank] & blackFileMask;
            result += pawnStormBonus[rank] * std::popcount(wp);
            result -= pawnStormBonus[rank] * std::popcount(bp);
        }

        // white king “queen mobility”
        tmp.clear();
        board.generateQueenMoves(tmp, board.whiteKing, board.whitePieces, board.blackPieces);
        int whiteKingProxy = tmp.size;

        if (whiteKingProxy <= 1) result -= (2 - whiteKingProxy) * 16;
        else if (whiteKingProxy > 3) result -= whiteKingProxy * 5;

        // black king “queen mobility”
        tmp.clear();
        board.generateQueenMoves(tmp, board.blackKing, board.blackPieces, board.whitePieces);
        int blackKingProxy = tmp.size;

        if (blackKingProxy <= 1) result += (2 - blackKingProxy) * 16;
        else if (blackKingProxy > 3) result += blackKingProxy * 5;
    }

    // material
    result += whiteMaterial;
    result -= blackMaterial;

    // PST
    result += posValWhite(board.whitePawns, pawn_pcsq);
    result += posValWhite(board.whiteKnights, knight_pcsq);
    result += posValWhite(board.whiteBishops, bishop_pcsq);

    result -= posValBlack(board.blackPawns, pawn_pcsq);
    result -= posValBlack(board.blackKnights, knight_pcsq);
    result -= posValBlack(board.blackBishops, bishop_pcsq);

    // king blend
    result += gamePhase * posValWhite(board.whiteKing, king_endgame_pcsq) +
              (1.0 - gamePhase) * posValWhite(board.whiteKing, king_pcsq);

    result -= gamePhase * posValBlack(board.blackKing, king_endgame_pcsq) +
              (1.0 - gamePhase) * posValBlack(board.blackKing, king_pcsq_black);

    // doubled pawns
    for (int f = 0; f < 8; ++f) {
        int wp = std::popcount(board.whitePawns & fileMasks[f]);
        int bp = std::popcount(board.blackPawns & fileMasks[f]);
        if (wp > 1) result -= 20 * (wp - 1);
        if (bp > 1) result += 20 * (bp - 1);
    }

    // late pawn progress + passed pawns (copied structure from engine2)
    if (gamePhase > 0.3) {
        double late = 0.0;
        for (int rank = 1; rank <= 6; ++rank) {
            uint64_t whiteRankPawns = board.whitePawns & rankMasks[rank];
            uint64_t blackRankPawns = board.blackPawns & rankMasks[7 - rank];

            late += pawnProgressBonus[rank] * std::popcount(whiteRankPawns);
            late -= pawnProgressBonus[rank] * std::popcount(blackRankPawns);

            for (int file = 0; file < 8; ++file) {
                // white passed pawns: no black pawns ahead on same/adj files
                uint64_t whitePawn = whiteRankPawns & fileMasks[file];
                if (whitePawn) {
                    uint64_t files =
                        fileMasks[file] |
                        (file > 0 ? fileMasks[file - 1] : 0ULL) |
                        (file < 7 ? fileMasks[file + 1] : 0ULL);

                    uint64_t ahead = rankMasks[rank + 1] | rankMasks[rank + 2] | rankMasks[rank + 3] |
                                     rankMasks[rank + 4] | rankMasks[rank + 5] | rankMasks[rank + 6];

                    uint64_t blockers = board.blackPawns & files & ahead;
                    if (!blockers) late += passedPawnBonus[rank];
                }

                // black passed pawns: no white pawns ahead (towards rank 0)
                uint64_t blackPawn = blackRankPawns & fileMasks[file];
                if (blackPawn) {
                    uint64_t files =
                        fileMasks[file] |
                        (file > 0 ? fileMasks[file - 1] : 0ULL) |
                        (file < 7 ? fileMasks[file + 1] : 0ULL);

                    int rBlack = 7 - rank;
                    uint64_t ahead = 0ULL;
                    for (int r2 = rBlack - 1; r2 >= 0; --r2) ahead |= rankMasks[r2];

                    uint64_t blockers = board.whitePawns & files & ahead;
                    if (!blockers) late -= passedPawnBonus[rank];
                }
            }
        }
        result += late * gamePhase * 1.5;
    }

    // pawns defending pawns (same)
    uint64_t leftDefW  = (board.whitePawns & ~fileMasks[7]) << 9;
    uint64_t rightDefW = (board.whitePawns & ~fileMasks[0]) << 7;
    result += 15 * std::popcount((leftDefW | rightDefW) & board.whitePawns);

    uint64_t leftDefB  = (board.blackPawns & ~fileMasks[7]) >> 7;
    uint64_t rightDefB = (board.blackPawns & ~fileMasks[0]) >> 9;
    result -= 15 * std::popcount((leftDefB | rightDefB) & board.blackPawns);

    // mobility
    // white king “queen mobility”
    tmp.clear();
    board.generateBishopMoves(tmp, board.whiteBishops, board.whitePieces, board.blackPieces);
    int numWhiteBishopMoves = tmp.size;
    result += 4 * numWhiteBishopMoves;

    tmp.clear();
    board.generateBishopMoves(tmp, board.blackBishops, board.blackPieces, board.whitePieces);
    int numBlackBishopMoves = tmp.size;
    result -= 4 * numBlackBishopMoves;


    tmp.clear();
    board.generateRookMoves(tmp, board.whiteRooks, board.whitePieces, board.blackPieces);
    int numWhiteRookMoves = tmp.size;
    result += 6 * numWhiteRookMoves;

    tmp.clear();
    board.generateRookMoves(tmp, board.blackRooks, board.blackPieces, board.whitePieces);
    int numBlackRookMoves = tmp.size;
    result -= 6 * numBlackRookMoves;

    tmp.clear();
    board.generateQueenMoves(tmp, board.whiteQueens, board.whitePieces, board.blackPieces);
    int numWhiteQueenMoves = tmp.size;
    result += 6 * numWhiteQueenMoves;

    tmp.clear();
    board.generateQueenMoves(tmp, board.blackQueens, board.blackPieces, board.whitePieces);
    int numBlackQueenMoves = tmp.size;
    result -= 6 * numBlackQueenMoves;

    // expand lead late + king distance (from engine2)
    if (gamePhase > 0.6 && std::abs(result) > 400.0) {
        result = result * (1.0 + gamePhase / 2.5);
        int distBetweenKingsBonus[9] = { 0, 0, 140, 80, 40, 20, 0, -10, -20 };
        int dist = kingDistance(board.blackKing, board.whiteKing);
        dist = std::clamp(dist, 0, 8);
        if (result > 0) result += distBetweenKingsBonus[dist];
        else result -= distBetweenKingsBonus[dist];
    }

    int s = (int)std::lround(result);
    return board.whiteToMove ? s : -s;
}

int Engine::quiescence(Board& board, int alpha, int beta, int ply, bool& timedOut) {
    // node accounting first so time masking works consistently
    lastNodes_++;

    if (((uint32_t)lastNodes_ & cfg_.timeCheckMask) == 0u) {
        if (outOfTime()) { timedOut = true; return 0; }
    }

    // Mate distance pruning window clamp (fail-soft friendly)
    alpha = std::max(alpha, -MATE_SCORE + ply);
    beta  = std::min(beta,   MATE_SCORE - ply);
    if (alpha >= beta) return alpha;

    if (ply >= 64) return evaluate(board);

    const bool inCheck = board.amIInCheck(board.whiteToMove);

    MoveList legal;
    board.generateAllMoves(legal);

    if (legal.size == 0) {
        return inCheck
            ? -(MATE_SCORE - ply)
            : ((board.whiteToMove == rootSideIsWhite_) ? -cfg_.drawPenalty : cfg_.drawPenalty);
    }

    if (!inCheck) {
        const int standPat = evaluate(board);
        if (standPat >= beta) return standPat;
        if (standPat > alpha) alpha = standPat;
    }

    // Build candidate list (fixed arrays, no heap)
    struct SM { Move m; int score; };
    SM cand[256];
    int n = 0;

    for (int i = 0; i < legal.size; ++i) {
        const Move& mv = legal.m[i];

        if (!inCheck) {
            // quiescence: captures/promotions only
            if (!mv.isCapture && !mv.promotion) continue;
        }

        int s = 0;
        if (mv.promotion) s += getPieceValue(mv.promotion) + 1000;
        if (mv.isCapture) s += isGoodCapture(mv, board);
        cand[n++] = { mv, s };
    }

    if (n == 0) return alpha;

    // selection-pick best each time
    for (int picked = 0; picked < n; ++picked) {
        int best = picked;
        int bestScore = cand[picked].score;
        for (int j = picked + 1; j < n; ++j) {
            if (cand[j].score > bestScore) {
                bestScore = cand[j].score;
                best = j;
            }
        }
        if (best != picked) std::swap(cand[best], cand[picked]);

        Move mv = cand[picked].m;
        Undo u;
        board.makeMove(mv, u);

        const int score = -quiescence(board, -beta, -alpha, ply + 1, timedOut);
        board.undoMove(mv, u);

        if (timedOut) return 0;

        if (score >= beta) return score;   // fail-soft cutoff
        if (score > alpha) alpha = score;
    }

    return alpha;
}

int Engine::search(Board& board, int depth, int alpha, int beta, int startDepth, int ply, int totalExtensions, bool lastIterationNull, Move& bestMoveOut, bool& timedOut){
    lastNodes_++;

    if (((uint32_t)lastNodes_ & cfg_.timeCheckMask) == 0u) {
        if (outOfTime()) { timedOut = true; bestMoveOut = NO_MOVE; return 0; }
    }

    // Mate distance pruning window clamp
    alpha = std::max(alpha, -MATE_SCORE + ply);
    beta  = std::min(beta,   MATE_SCORE - ply);
    if (alpha >= beta) { bestMoveOut = NO_MOVE; return alpha; }

    if (depth <= 0) {
        bestMoveOut = NO_MOVE;
        return quiescence(board, alpha, beta, ply, timedOut);
    }

    if (ply > 0 && !lastIterationNull && board.isThreefoldRepetition()) {
        bestMoveOut = NO_MOVE;
        return (board.whiteToMove == rootSideIsWhite_) ? -cfg_.drawPenalty : cfg_.drawPenalty;
    }

    const int originalAlpha = alpha;
    const int originalBeta  = beta;
    bestMoveOut = NO_MOVE;

    const uint64_t key = board.zobristHash;

    // TT probe
    EngineTTEntry* tt = probeTT(key);
    Move hashMove = NO_MOVE;

    if (tt && tt->key == key) {
        hashMove = tt->move;
        const int ttScore = scoreFromTT(tt->score, ply);

        if (tt->depth >= depth) {
            if (tt->flag == HASH_FLAG_EXACT) {
                bestMoveOut = tt->move;
                return ttScore;
            }
            if (tt->flag == HASH_FLAG_LOWER) alpha = std::max(alpha, ttScore);
            if (tt->flag == HASH_FLAG_UPPER) beta  = std::min(beta,  ttScore);
            if (alpha >= beta) {
                bestMoveOut = tt->move;
                return ttScore;
            }
        }
    }

    const bool inCheck = board.amIInCheck(board.whiteToMove);

    // Null-move pruning
    if (!inCheck && !lastIterationNull && depth >= 3 && std::abs(beta) < (MATE_THRESHOLD - 500) && isNullViable(board)){
        Undo nu;
        board.makeNullMove(nu);

        Move dummy = NO_MOVE;
        const int R = cfg_.nullMoveReductionBase + (depth / 3);
        const int nullDepth = depth - 1 - R;

        int score = -search(board,
                            nullDepth,
                            -(beta),
                            -(beta - 1),
                            startDepth,
                            ply + 1,
                            totalExtensions,
                            true,
                            dummy,
                            timedOut);

        board.undoNullMove(nu);

        if (timedOut) { bestMoveOut = NO_MOVE; return 0; }

        if (score >= beta) {
            // FAIL-SOFT: return the real score so PVS can detect improvement correctly
            bestMoveOut = NO_MOVE;
            return score;
        }
    }

    MoveList moves;
    board.generateAllMoves(moves);

    if (moves.size == 0) {
        bestMoveOut = NO_MOVE;
        return inCheck
            ? -(MATE_SCORE - ply)
            : ((board.whiteToMove == rootSideIsWhite_) ? -cfg_.drawPenalty : cfg_.drawPenalty);
    }

    // Quiet tried list
    Move quietTried[64];
    int quietTriedN = 0;

    int bestScore = -1000000;

    if (ply >= MAX_PLY) {
        bestMoveOut = NO_MOVE;
        return evaluate(board);
    }

    EngineMovePicker picker(board, moves, hashMove, killers_[0][ply], killers_[1][ply], true, history_);

    Move mv;
    int moveIndex = 0;

    while (true) {
        if (!picker.next(mv)) break;

        Undo u;
        Move played = mv;
        board.makeMove(played, u);

        const bool quiet = (!played.isCapture && !played.promotion);
        if (quiet && quietTriedN < 64) {
            quietTried[quietTriedN++] = played;
        }

        int ext = 0;
        if (totalExtensions < cfg_.maxExtensionsPerLine) {
            if (board.amIInCheck(board.whiteToMove)) ext = 1;
        }
        ext = std::clamp(ext, 0, 2);

        int reduction = 0;
        if (depth >= 4 && quiet && ext == 0 && moveIndex >= 3 && std::abs(alpha) < (MATE_THRESHOLD - 500) && !board.amIInCheck(board.whiteToMove)){
            reduction = 1;
        }

        Move childBest = NO_MOVE;
        int score;

        // PVS
        if (moveIndex > 0) {
            const int child = search(board,
                                     depth - 1 - reduction + ext,
                                     -(alpha + 1),
                                     -alpha,
                                     startDepth,
                                     ply + 1,
                                     totalExtensions + ext,
                                     false,
                                     childBest,
                                     timedOut);

            score = -child;

            // IMPORTANT: with fail-soft scores, this condition behaves properly
            if (!timedOut && score > alpha && score < beta) {
                score = -search(board,
                                depth - 1 - reduction + ext,
                                -beta,
                                -alpha,
                                startDepth,
                                ply + 1,
                                totalExtensions + ext,
                                false,
                                childBest,
                                timedOut);
            }
        } else {
            score = -search(board,
                            depth - 1 - reduction + ext,
                            -beta,
                            -alpha,
                            startDepth,
                            ply + 1,
                            totalExtensions + ext,
                            false,
                            childBest,
                            timedOut);
        }

        // LMR re-search
        if (!timedOut && reduction == 1 && score > alpha) {
            Move retryBest = NO_MOVE;
            score = -search(board,
                            depth - 1 + ext,
                            -beta,
                            -alpha,
                            startDepth,
                            ply + 1,
                            totalExtensions + ext,
                            false,
                            retryBest,
                            timedOut);
        }

        board.undoMove(played, u);

        if (timedOut) { bestMoveOut = NO_MOVE; return 0; }

        if (score > bestScore) {
            bestScore = score;
            bestMoveOut = mv;
        }

        if (score > alpha) alpha = score;

        if (alpha >= beta) {
            if (quiet) {
                recordKiller(mv, ply);

                const int bonus = depth * depth;
                const int malus = bonus / 4;

                updateHistory(board, mv.from, mv.to, bonus);

                for (int qi = 0; qi < quietTriedN; ++qi) {
                    const Move& q = quietTried[qi];
                    if (q == mv) continue;
                    updateHistory(board, q.from, q.to, -malus);
                }
            }
            break;
        }

        moveIndex++;
    }

    // TT store
    TTFlag flag;
    if (bestScore <= originalAlpha) flag = HASH_FLAG_UPPER;
    else if (bestScore >= originalBeta) flag = HASH_FLAG_LOWER;
    else flag = HASH_FLAG_EXACT;

    storeTT(key, scoreToTT(bestScore, ply), flag, bestMoveOut, depth);

    return bestScore;
}

void Engine::setTimeLimitMs(int ms) {
    if (ms < 1) ms = 1;
    if (ms > 20000) ms = 20000;
    cfg_.timeLimitMs = ms;
}

int Engine::getTimeLimitMs() const {
    return cfg_.timeLimitMs;
}

Move Engine::getMove(Board& board) {
    endTime_ = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(cfg_.timeLimitMs);
    rootSideIsWhite_ = board.whiteToMove;
    lastNodes_ = 0;
    lastDepth_ = 0;
    lastEval_  = 0;

    Move bestMove = NO_MOVE;
    int bestScore = 0;

    Move prevBest = NO_MOVE;
    int prevScore = 0;

    bool timedOut = false;

    auto matePlies = [&](int s) -> int {
        if (std::abs(s) < MATE_THRESHOLD) return 999999;
        return MATE_SCORE - std::abs(s); // plies to mate from root
    };

    for (int depth = 1; depth <= cfg_.maxDepth; ++depth) {
        Move rootBest = NO_MOVE;
        timedOut = false;

        // If we're already in mate territory, aspiration is more harm than help.
        const bool prevIsMate = (std::abs(prevScore) >= MATE_THRESHOLD);

        int alpha = -999999;
        int beta  =  999999;

        if (depth > 1 && !prevIsMate) {
            int window = cfg_.aspirationStartWindow;
            alpha = prevScore - window;
            beta  = prevScore + window;

            if (cfg_.useOpeningBook) {
                static OpeningBook book;
                static bool loaded = false;

                if (!loaded) {
                    book.load("opening_book.bin");
                    loaded = true;
                }

                Move bookMove;
                if (book.probe(board.zobristHash, bookMove)) {
                    std::cout << "Used opening book" << std::endl;
                    return bookMove;
                }
            }


            while (true) {
                rootBest = NO_MOVE;
                int score = search(board, depth, alpha, beta, depth, 0, 0, false, rootBest, timedOut);
                if (timedOut) break;

                // fail-low => widen down
                if (score <= alpha) {
                    window *= cfg_.aspirationGrowFactor;
                    alpha = prevScore - window;
                    beta  = prevScore + window;
                    continue;
                }

                // fail-high => widen up
                if (score >= beta) {
                    window *= cfg_.aspirationGrowFactor;
                    alpha = prevScore - window;
                    beta  = prevScore + window;
                    continue;
                }

                // success
                prevScore = score;
                break;
            }

            if (timedOut) break;

            if (rootBest.from != -1 && rootBest.to != -1) {
                bestMove = rootBest;
                bestScore = prevScore;
                lastDepth_ = depth;
                lastEval_ = bestScore;
            } else {
                bestMove = prevBest;
                bestScore = prevScore;
            }
        } else {
            int score = search(board, depth, alpha, beta, depth, 0, 0, false, rootBest, timedOut);
            if (timedOut) break;

            if (rootBest.from != -1 && rootBest.to != -1) {
                bestMove = rootBest;
                bestScore = score;
                lastDepth_ = depth;
                lastEval_ = bestScore;
            } else {
                bestMove = prevBest;
                bestScore = prevScore;
            }

            prevScore = bestScore;
        }

        prevBest = bestMove;

        // Mate early exit: only stop once depth is sufficient to prove the mate distance.
        const int m = matePlies(bestScore);
        if (m != 999999) {
            if (depth >= m) break;
        }

        if (outOfTime()) break;
    }

    return bestMove;
}

size_t Engine::transpositionSize() const {
    size_t used = 0;
    for (const auto& e : tt_) {
        if (e.depth != -1) ++used;
    }
    return used;
}

void Engine::printAfterMoveDebug(Engine& engine, Board& board) {
    std::cout << "=====================Engine Move====================\n";
    std::cout << "Mover: " << (engine.rootSideIsWhite_ ? "White" : "Black") << "\n";
    std::cout << "Search depth reached: " << engine.lastDepth_ << "\n";
    std::cout << "Positions evaluated: " << engine.lastNodes_ << "\n";
    std::cout << "Eval: " << engine.lastEval_ << "\n";
    board.printBoard(); 
    std::cout << "====================================================\n";
}