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

typedef uint64_t Bitboard;

#define NO_HASH_ENTRY       0
#define USE_HASH_MOVE       1
#define RETURN_HASH_SCORE   2


class Move {
public:
    int from;
    int to;
    char promotion;
    bool isCapture;
    char capturedPiece;

    // Default constructor
    Move() : from(-1), to(-1), promotion(0), isCapture(false), capturedPiece(0) {}

    Move(int f, int t, char p = 0, bool e = false, bool c = false, char captured = 0)
        : from(f), to(t), promotion(p), isCapture(c), capturedPiece(captured) {}

    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promotion == other.promotion &&
            isCapture == other.isCapture && capturedPiece == other.capturedPiece;
    }

    bool operator!=(const Move& other) const {
        return !(*this == other);
    }
};

extern const Move NO_MOVE;

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

    std::unordered_map<uint64_t, int> positionHistory;
    Move killerMoves[2][64]; // Two killer moves per depth, up to depth of 64
    int64_t historyHeuristic[12][64];
    int64_t maxHistoryValue;

    Board();
    void createBoard();
    void createBoardFromFEN(const std::string& fen);
    void printBoard();
    std::vector<Move> generatePawnMoves(Bitboard pawns, Bitboard ownPieces, Bitboard opponentPieces);
    std::vector<Move> generateBishopMoves(Bitboard bishops, Bitboard ownPieces, Bitboard opponentPieces);
    std::vector<Move> generateRookMoves(Bitboard rooks, Bitboard ownPieces, Bitboard opponentPieces);
    std::vector<Move> generateKnightMoves(Bitboard knights, Bitboard ownPieces, Bitboard opponentPieces);
    std::vector<Move> generateKingMoves(Bitboard king, Bitboard ownPieces, Bitboard opponentPieces);
    std::vector<Move> generateQueenMoves(Bitboard queens, Bitboard ownPieces, Bitboard opponentPieces);
    std::vector<Move> generateAllMoves();
    bool amIInCheck(bool player);
    void makeMove(Move& move);
    void undoMove(const Move& move);
    bool isSquareAttacked(int square, bool byWhite);
    char getPieceAt(int index) const;

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
    void makeNullMove();
    void undoNullMove();
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
