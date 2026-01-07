#pragma once
#include <vector>
#include <tuple>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <chrono>
#include <string>
#include <deque>
#include <random>
#include "zobrist.h"
#include <tuple>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <array>

typedef uint64_t Bitboard;

#define NO_HASH_ENTRY       0
#define USE_HASH_MOVE       1
#define RETURN_HASH_SCORE   2

struct Move {
    int  from;
    int  to;
    char promotion;   // 0 or 'q','r','b','n'
    bool isCapture;
};

inline constexpr Move NO_MOVE{ -1, -1, 0, false };

inline constexpr bool operator==(const Move& a, const Move& b) noexcept {
    return a.from == b.from && a.to == b.to && a.promotion == b.promotion;
}
inline constexpr bool operator!=(const Move& a, const Move& b) noexcept {
    return !(a == b);
}

struct Undo {
    // Zobrist hash (restore on undo)
    uint64_t prevHash = 0;
    int prevRepIrrevIndex = 0;

    // EP state
    Bitboard prevEnPassantTarget = 0;
    int prevEpFile = -1;

    // Castling-right flags (your engine uses these booleans as "moved" flags)
    bool prevWhiteKingMoved  = false;
    bool prevWhiteLRookMoved = false;
    bool prevWhiteRRookMoved = false;
    bool prevBlackKingMoved  = false;
    bool prevBlackLRookMoved = false;
    bool prevBlackRRookMoved = false;

    // Capture info
    char capturedPiece = 0;   // 'p','n','b','r','q','k' or 0
    bool wasEnPassant  = false;

    // mailbox undo info:
    char movedPieceChar = ' ';      // char that was on move.from before the move
    char capturedPieceChar = ' ';   // char that was captured (exact char), or ' '
    int  capturedSquare = -1;       // square the victim was on (EP uses behind-square), or -1
};

enum TTFlag {
    HASH_FLAG_EXACT,  // Exact score
    HASH_FLAG_LOWER,  // Lower bound score
    HASH_FLAG_UPPER,  // Upper bound score
    HASH_BOOK
};

struct TT_Entry { 
    uint64_t key;     // Zobrist key of the position
    Move move;        // Best move from this position
    int score;        // Evaluation score
    int depth;        // Depth at which the position was evaluated
    TTFlag flag;      // Type of node

    TT_Entry() : key(0), score(0), depth(0), flag(HASH_FLAG_EXACT), move(NO_MOVE) {}
};

struct MoveList {
    Move m[256];
    int size = 0;

    inline void clear() { size = 0; }

    inline void push(const Move& mv) { m[size++] = mv; }

    inline Move* begin() { return m; }
    inline Move* end()   { return m + size; }

    inline const Move* begin() const { return m; }
    inline const Move* end()   const { return m + size; }
};

struct ScoredMove {
    Move m;
    int score;
};

class Board {
public:
    Bitboard whitePawns;
    Bitboard blackPawns;
    Bitboard whiteBishops;
    Bitboard blackBishops;
    Bitboard whiteRooks;
    Bitboard blackRooks;
    Bitboard whiteKnights;
    Bitboard blackKnights;
    Bitboard whiteQueens;
    Bitboard blackQueens;
    Bitboard whiteKing;
    Bitboard blackKing;
    Bitboard whitePieces;
    Bitboard blackPieces;
    Bitboard enPassantTarget;

    Bitboard enPassantPrev;
    bool whiteKingMovedPrev;
    bool whiteLRookMovedPrev;
    bool whiteRRookMovedPrev;
    bool blackKingMovedPrev;
    bool blackLRookMovedPrev;
    bool blackRRookMovedPrev;

    bool whiteToMove;

    bool whiteKingMoved;
    bool whiteLRookMoved;
    bool whiteRRookMoved;

    bool blackKingMoved;
    bool blackLRookMoved;
    bool blackRRookMoved;

    Move lastMove;
    int epFile;

    uint64_t zobristHash; 
    std::unordered_map<uint64_t, int> positionHistory;
    Move killerMoves[2][64]; // Two killer moves per depth, up to depth of 64
    int64_t historyHeuristic[12][64];
    int64_t maxHistoryValue;

    static constexpr int MAX_REP_PLY = 2048;
    std::array<uint64_t, MAX_REP_PLY> repStack{};
    int repPly = 0;
    int repIrrevIndex = 0;

    std::array<char, 64> pieceAt;   // 'p','n'...'k' for white, 'P'...'K' for black, ' ' empty

    Board();
    void createBoard();
    void createBoardFromFEN(const std::string& fen);
    void printBoard();
    void generatePawnMoves(MoveList& moves, Bitboard pawns, Bitboard ownPieces, Bitboard opponentPieces);
    void generateBishopMoves(MoveList& moves, Bitboard bishops, Bitboard ownPieces, Bitboard opponentPieces);
    void generateRookMoves(MoveList& moves, Bitboard rooks, Bitboard ownPieces, Bitboard opponentPieces);
    void generateKnightMoves(MoveList& moves, Bitboard knights, Bitboard ownPieces, Bitboard opponentPieces);
    void generateKingMoves(MoveList& moves, Bitboard king, Bitboard ownPieces, Bitboard opponentPieces);
    void generateQueenMoves(MoveList& moves, Bitboard queens, Bitboard ownPieces, Bitboard opponentPieces);
    void generateAllMoves(MoveList& moves);
    bool amIInCheck(bool player);
    void makeMove(Move& move, Undo& u);
    void undoMove(const Move& move, const Undo& u);
    char getPieceAt(int index) const;
    Bitboard computePinnedMask(bool forWhite) const;
    void rebuildMailbox(); 

    void updatePositionHistory(bool plus);
    bool isThreefoldRepetition();
    bool isThreefoldRepetition(uint64_t hash);
    int getPieceIndex(char piece) const;
    int getEnPassantFile() const;
    uint64_t generateZobristHash() const;

    void makeNullMove(Undo& u);
    void undoNullMove(const Undo& u);
    int posToValue(int from);
    void updateHistory(int from, int to, int bonus);
    void loadOpeningBook();
};

// Helper functions
void setBit(Bitboard& bitboard, int square);
void parseFEN(const std::string& fen, Board& board);
std::string numToBoardPosition(int num);
bool isTacticalPosition(const std::vector<Move>& moves, const Board& board);
bool isNullViable(Board& board);
Move convertToMoveObject(const std::string& moveStr);
int boardPositionToIndex(const std::string& pos);
int isGoodCapture(const Move& move, const Board& board);
bool isEqualCapture(const Move& move, const Board& board);
int getPieceValue(char piece);

class MovePicker {
public:
    MovePicker(Board& b, const std::vector<Move>& moves, TT_Entry* ttEntry, int depth)
        : board(b), d(depth)
    {
        ttMove = (ttEntry && ttEntry->depth >= (depth >> 1)) ? ttEntry->move : NO_MOVE;
        hasTT = !(ttMove == NO_MOVE);

        killer1 = board.killerMoves[0][depth];
        killer2 = board.killerMoves[1][depth];

        goodCaps.reserve(moves.size());
        badCaps.reserve(moves.size());
        quiets.reserve(moves.size());
        killers.reserve(2);

        for (const Move& mv : moves) {
            if (hasTT && mv == ttMove) continue;

            if (mv.isCapture || mv.promotion) {
                int s = isGoodCapture(mv, board);
                if (mv.promotion) s += getPieceValue(mv.promotion) + 1000;

                if (s >= 0 || mv.promotion) goodCaps.push_back({ mv, s });
                else                        badCaps.push_back({ mv, s });
            }
            else if (mv == killer1 || mv == killer2) {
                killers.push_back(mv);
            }
            else {
                int s = (int)board.historyHeuristic[board.posToValue(mv.from)][mv.to];
                quiets.push_back({ mv, s });
            }
        }
    }

    bool next(Move& out) {
        // stage 0: TT move
        if (!ttDone) {
            ttDone = true;
            if (hasTT) { out = ttMove; return true; }
        }

        // stage 1: good captures/promos
        if (pickBestFrom(goodCaps, goodIdx, out)) return true;

        // stage 2: killers
        while (killerIdx < (int)killers.size()) {
            out = killers[killerIdx++];
            return true;
        }

        // stage 3: quiets by history
        if (pickBestFrom(quiets, quietIdx, out)) return true;

        // stage 4: bad captures last
        if (pickBestFrom(badCaps, badIdx, out)) return true;

        return false;
    }

private:
    Board& board;
    int d;

    Move ttMove = NO_MOVE;
    bool hasTT = false;
    bool ttDone = false;

    Move killer1 = NO_MOVE, killer2 = NO_MOVE;
    std::vector<Move> killers;
    int killerIdx = 0;

    std::vector<ScoredMove> goodCaps, badCaps, quiets;
    int goodIdx = 0, badIdx = 0, quietIdx = 0;

    static inline bool pickBestFrom(std::vector<ScoredMove>& v, int& idx, Move& out) {
        if (idx >= (int)v.size()) return false;

        int best = idx;
        int bestScore = v[idx].score;

        for (int i = idx + 1; i < (int)v.size(); ++i) {
            if (v[i].score > bestScore) {
                bestScore = v[i].score;
                best = i;
            }
        }

        if (best != idx) std::swap(v[best], v[idx]);
        out = v[idx].m;
        ++idx;
        return true;
    }
};
