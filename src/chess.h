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


class Move {
public:
    int from;
    int to;
    char promotion;   // 'q','r','b','n' or 0
    bool isCapture;

    Move() : from(-1), to(-1), promotion(0), isCapture(false) {}
    Move(int f, int t, char p = 0) : from(f), to(t), promotion(p), isCapture(false) {}

    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promotion == other.promotion;
    }
    bool operator!=(const Move& other) const { return !(*this == other); }
};

extern const Move NO_MOVE;

struct Undo {
    // Zobrist hash (restore on undo)
    uint64_t prevHash = 0;

    // EP state
    Bitboard prevEnPassantTarget = 0;

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

    inline void push(const Move& mv) {
        m[size++] = mv;
    }

    inline Move* begin() { return m; }
    inline Move* end()   { return m + size; }

    inline const Move* begin() const { return m; }
    inline const Move* end()   const { return m + size; }
};

struct ScoredMove {
    Move mv;
    uint64_t score;
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

    uint64_t zobristHash; 
    std::unordered_map<uint64_t, int> positionHistory;
    Move killerMoves[2][64]; // Two killer moves per depth, up to depth of 64
    int64_t historyHeuristic[12][64];
    int64_t maxHistoryValue;

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
    void makeMoveFast(Move& move, Undo& u);
    void undoMoveFast(const Move& move, const Undo& u);
    char getPieceAt(int index) const;
    Bitboard computePinnedMask(bool forWhite) const;
    void rebuildMailbox(); 

    void updatePositionHistory(bool plus);
    bool isThreefoldRepetition();
    bool isThreefoldRepetition(uint64_t hash);
    int getPieceIndex(char piece) const;
    int getEnPassantFile() const;
    uint64_t generateZobristHash() const;

    std::vector<TT_Entry> transposition_table;
    void resize_tt(uint64_t mb);
    void clear_tt();
    void record_tt_entry(uint64_t hash_key, int score, TTFlag flag, Move move, int depth);
    void configureTranspositionTableSize(uint64_t sizeInMB);
    short probe_tt_entry(uint64_t hash_key, int alpha, int beta, int depth, TT_Entry& return_entry);
    TT_Entry* probeTranspositionTable(uint64_t hash);
    size_t countTranspositionTableEntries() const;
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
std::vector<std::pair<Move, uint64_t>> orderMoves(Board& board, const std::vector<Move>& moves, TT_Entry* ttEntry, int depth);
std::vector<Move> orderMoves2(Board& board, const std::vector<Move>& moves, TT_Entry* ttEntry, int depth);
bool isTacticalPosition(std::vector<Move> moves, Board board);
bool isNullViable(Board& board);
Move convertToMoveObject(const std::string& moveStr);
int boardPositionToIndex(const std::string& pos);
int isGoodCapture(const Move& move, const Board& board);
bool isEqualCapture(const Move& move, const Board& board);
int getPieceValue(char piece);
