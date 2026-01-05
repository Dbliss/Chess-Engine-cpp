#include "chess.h"
#include <iostream>
#include <sstream>
#include <string>
#include <intrin.h>
#include <chrono>
#include "zobrist.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <cmath>
#include <bit>
#include <immintrin.h>

constexpr Bitboard NOT_A_FILE = 0x7F7F7F7F7F7F7F7FULL;
constexpr Bitboard NOT_H_FILE = 0xFEFEFEFEFEFEFEFEULL;

enum DirIndex : int {
    DIR_N  = 0,  // +8
    DIR_S  = 1,  // -8
    DIR_E  = 2,  // +1
    DIR_W  = 3,  // -1
    DIR_NE = 4,  // +9
    DIR_NW = 5,  // +7
    DIR_SE = 6,  // -7
    DIR_SW = 7   // -9
};

static constexpr int DIR_DELTA[8] = { 8, -8, 1, -1, 9, 7, -7, -9 };

alignas(64) static Bitboard KNIGHT_ATTACKS[64];
alignas(64) static Bitboard KING_ATTACKS[64];
// Squares where an attacker pawn must sit to attack TARGET square.
// index 0 = attackers are WHITE, index 1 = attackers are BLACK (matches your "attackersAreWhite" bool)
alignas(64) static Bitboard PAWN_ATTACKERS[2][64];

alignas(64) static Bitboard RAY[8][64]; // squares outward from sq in that dir (excluding sq)

static inline int lsb_index(Bitboard b) {
    unsigned long idx;
    _BitScanForward64(&idx, b);
    return (int)idx;
}

static inline int msb_index(Bitboard b) {
    unsigned long idx;
    _BitScanReverse64(&idx, b);
    return (int)idx;
}

static inline int pop_lsb(Bitboard& b) {
    unsigned long idx;
    _BitScanForward64(&idx, b);
    b &= (b - 1);
    return (int)idx;
}

static inline bool step_ok_noinline(int from, int dir, int& to) {
    to = from + dir;
    if (to < 0 || to >= 64) return false;

    // Match your rook wrap logic
    if (dir == 1  && (to % 8 == 0)) return false;
    if (dir == -1 && (to % 8 == 7)) return false;

    // Match your bishop wrap logic
    if ((to % 8 == 0 && (dir == 9 || dir == -7)) ||
        (to % 8 == 7 && (dir == 7 || dir == -9)))
        return false;

    return true;
}

static void init_attack_tables() {
    // clear
    for (int i = 0; i < 64; ++i) {
        KNIGHT_ATTACKS[i] = 0;
        KING_ATTACKS[i] = 0;
        PAWN_ATTACKERS[0][i] = 0;
        PAWN_ATTACKERS[1][i] = 0;
        for (int d = 0; d < 8; ++d) RAY[d][i] = 0;
    }

    // rays
    for (int sq = 0; sq < 64; ++sq) {
        for (int d = 0; d < 8; ++d) {
            int cur = sq;
            int t;
            while (step_ok_noinline(cur, DIR_DELTA[d], t)) {
                RAY[d][sq] |= (1ULL << t);
                cur = t;
            }
        }
    }

    // knight / king (use the exact checks you already rely on: abs(file diff) cap)
    {
        static constexpr int kSteps[8]  = { 17, 15, 10, 6, -17, -15, -10, -6 };
        static constexpr int kiSteps[8] = { 8, -8, 1, -1, 9, 7, -9, -7 };

        for (int sq = 0; sq < 64; ++sq) {
            for (int s : kSteps) {
                int to = sq + s;
                if (to >= 0 && to < 64 && std::abs((sq % 8) - (to % 8)) <= 2) {
                    KNIGHT_ATTACKS[sq] |= (1ULL << to);
                }
            }
            for (int s : kiSteps) {
                int to = sq + s;
                if (to >= 0 && to < 64 && std::abs((sq % 8) - (to % 8)) <= 1) {
                    KING_ATTACKS[sq] |= (1ULL << to);
                }
            }
        }
    }

    // pawn attackers (invert your existing pawn-attack masking rules)
    for (int target = 0; target < 64; ++target) {
        const Bitboard tMask = 1ULL << target;

        // attackersAreWhite == true: pawns attack via +7 and +9, and you mask the RESULT with NOT_A_FILE / NOT_H_FILE
        // So target must survive that mask for the attack to be valid.
        if (target - 7 >= 0 && (tMask & NOT_A_FILE)) PAWN_ATTACKERS[0][target] |= (1ULL << (target - 7));
        if (target - 9 >= 0 && (tMask & NOT_H_FILE)) PAWN_ATTACKERS[0][target] |= (1ULL << (target - 9));

        // attackersAreWhite == false (black): you use >>7 masked with NOT_H_FILE, and >>9 masked with NOT_A_FILE
        if (target + 7 < 64 && (tMask & NOT_H_FILE)) PAWN_ATTACKERS[1][target] |= (1ULL << (target + 7));
        if (target + 9 < 64 && (tMask & NOT_A_FILE)) PAWN_ATTACKERS[1][target] |= (1ULL << (target + 9));
    }
}

static inline void init_attack_tables_once() {
    static bool inited = false;
    if (!inited) {
        init_attack_tables();
        inited = true;
    }
}

static inline int first_blocker_sq(Bitboard blockers, int dirDelta) {
    // all your +delta dirs increase square index; -delta dirs decrease
    return (dirDelta > 0) ? lsb_index(blockers) : msb_index(blockers);
}

alignas(64) static Bitboard ROOK_MASK[64];
alignas(64) static Bitboard BISHOP_MASK[64];
static std::vector<Bitboard> ROOK_TAB[64];
static std::vector<Bitboard> BISHOP_TAB[64];

static Bitboard rook_mask_sq(int sq) {
    // mask without edges (standard)
    Bitboard m = 0;
    int r = sq / 8, f = sq % 8;
    for (int rr=r+1; rr<=6; rr++) m |= 1ULL << (rr*8+f);
    for (int rr=r-1; rr>=1; rr--) m |= 1ULL << (rr*8+f);
    for (int ff=f+1; ff<=6; ff++) m |= 1ULL << (r*8+ff);
    for (int ff=f-1; ff>=1; ff--) m |= 1ULL << (r*8+ff);
    return m;
}

static Bitboard bishop_mask_sq(int sq) {
    Bitboard m = 0;
    int r = sq / 8, f = sq % 8;
    for (int rr=r+1, ff=f+1; rr<=6 && ff<=6; rr++,ff++) m |= 1ULL<<(rr*8+ff);
    for (int rr=r+1, ff=f-1; rr<=6 && ff>=1; rr++,ff--) m |= 1ULL<<(rr*8+ff);
    for (int rr=r-1, ff=f+1; rr>=1 && ff<=6; rr--,ff++) m |= 1ULL<<(rr*8+ff);
    for (int rr=r-1, ff=f-1; rr>=1 && ff>=1; rr--,ff--) m |= 1ULL<<(rr*8+ff);
    return m;
}


static inline __forceinline Bitboard rook_attacks(int sq, Bitboard occ) {
    uint64_t idx = _pext_u64(occ, ROOK_MASK[sq]);
    return ROOK_TAB[sq][idx];
}
static inline __forceinline Bitboard bishop_attacks(int sq, Bitboard occ) {
    uint64_t idx = _pext_u64(occ, BISHOP_MASK[sq]);
    return BISHOP_TAB[sq][idx];
}

static inline Bitboard rook_attacks_slow(int from, Bitboard occ) {
    Bitboard a = 0;
    const int dirs[4] = { DIR_N, DIR_S, DIR_E, DIR_W };
    for (int di : dirs) {
        Bitboard ray = RAY[di][from];
        Bitboard blockers = ray & occ;
        if (blockers) {
            int bSq = first_blocker_sq(blockers, DIR_DELTA[di]);
            a |= (ray & ~RAY[di][bSq]);
        } else {
            a |= ray;
        }
    }
    return a;
}

static inline Bitboard bishop_attacks_slow(int from, Bitboard occ) {
    Bitboard a = 0;
    const int dirs[4] = { DIR_NE, DIR_NW, DIR_SE, DIR_SW };
    for (int di : dirs) {
        Bitboard ray = RAY[di][from];
        Bitboard blockers = ray & occ;
        if (blockers) {
            int bSq = first_blocker_sq(blockers, DIR_DELTA[di]);
            a |= (ray & ~RAY[di][bSq]);
        } else {
            a |= ray;
        }
    }
    return a;
}

static void init_slider_pext_tables() {
    for (int sq=0; sq<64; ++sq) {
        ROOK_MASK[sq] = rook_mask_sq(sq);
        BISHOP_MASK[sq] = bishop_mask_sq(sq);

        const int rBits = std::popcount(ROOK_MASK[sq]);
        const int bBits = std::popcount(BISHOP_MASK[sq]);

        ROOK_TAB[sq].assign(1ULL<<rBits, 0);
        BISHOP_TAB[sq].assign(1ULL<<bBits, 0);

        // enumerate subsets (carry-rippler)
        Bitboard m = ROOK_MASK[sq];
        for (Bitboard subset = m;; subset = (subset - 1) & m) {
            uint64_t idx = _pext_u64(subset, m);
            ROOK_TAB[sq][idx] = rook_attacks_slow(sq, subset); // reuse your correct generator
            if (!subset) break;
        }

        m = BISHOP_MASK[sq];
        for (Bitboard subset = m;; subset = (subset - 1) & m) {
            uint64_t idx = _pext_u64(subset, m);
            BISHOP_TAB[sq][idx] = bishop_attacks_slow(sq, subset);
            if (!subset) break;
        }
    }
}

static inline __forceinline bool isSquareAttacked_fast(
    int targetSquare,
    bool attackersAreWhite,
    Bitboard occupiedSquares,
    Bitboard attackerPawns,
    Bitboard attackerKnights,
    Bitboard attackerBishops,
    Bitboard attackerRooks,
    Bitboard attackerQueens,
    Bitboard attackerKing
) {
    const int pawnIdx = attackersAreWhite ? 0 : 1;

    // pawns (no shifting)
    if (attackerPawns & PAWN_ATTACKERS[pawnIdx][targetSquare]) return true;

    // knights / king (precomputed)
    if (attackerKnights & KNIGHT_ATTACKS[targetSquare]) return true;
    if (attackerKing    & KING_ATTACKS[targetSquare])   return true;

    // sliders via PEXT attack tables
    const Bitboard rookQueen = attackerRooks   | attackerQueens;
    const Bitboard bishQueen = attackerBishops | attackerQueens;

    // NOTE: bishop_attacks/rook_attacks already account for blockers in occupiedSquares
    if (bishQueen & bishop_attacks(targetSquare, occupiedSquares)) return true;
    if (rookQueen & rook_attacks(targetSquare, occupiedSquares))   return true;

    return false;
}

static inline char pieceTypeLower(char c) {
    // returns 'p','n','b','r','q','k' regardless of color case
    if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return c;
}

static inline bool step_ok(int from, int dir, int& to) {
    to = from + dir;
    if (to < 0 || to >= 64) return false;

    // Match YOUR rook wrap logic
    if (dir == 1  && (to % 8 == 0)) return false;
    if (dir == -1 && (to % 8 == 7)) return false;

    // Match YOUR bishop wrap logic (as used in generateBishopMoves)
    if ((to % 8 == 0 && (dir == 9 || dir == -7)) ||
        (to % 8 == 7 && (dir == 7 || dir == -9)))
        return false;

    return true;
}

static inline bool isSquareAttacked(int targetSquare, bool attackersAreWhite, Bitboard occupiedSquares, Bitboard attackerPawns, Bitboard attackerKnights, Bitboard attackerBishops, Bitboard attackerRooks, Bitboard attackerQueens, Bitboard attackerKing) {
    const Bitboard targetMask = 1ULL << targetSquare;

    // Pawn attacks (match your engine's shift directions)
    Bitboard pawnAttacks = attackersAreWhite
        ? (((attackerPawns << 7) & NOT_A_FILE) | ((attackerPawns << 9) & NOT_H_FILE))
        : (((attackerPawns >> 7) & NOT_H_FILE) | ((attackerPawns >> 9) & NOT_A_FILE));

    if (pawnAttacks & targetMask) return true;

    // Knight attacks
    const int knightSteps[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };
    for (int step : knightSteps) {
        int sq = targetSquare + step;
        if (sq >= 0 && sq < 64 && std::abs((targetSquare % 8) - (sq % 8)) <= 2) {
            if (attackerKnights & (1ULL << sq)) return true;
        }
    }

    // Bishop/queen diagonal attacks (use occupiedSquares for blockers)
    const int bishopDirs[4] = { 9, 7, -9, -7 };
    for (int dir : bishopDirs) {
        int sq = targetSquare;
        while (true) {
            sq += dir;
            if (sq < 0 || sq >= 64 ||
                (sq % 8 == 0 && (dir == 9 || dir == -7)) ||
                (sq % 8 == 7 && (dir == 7 || dir == -9))) break;

            Bitboard sqMask = 1ULL << sq;
            if ((attackerBishops | attackerQueens) & sqMask) return true;
            if (occupiedSquares & sqMask) break; // blocked by any piece
        }
    }

    // Rook/queen straight attacks (use occupiedSquares for blockers)
    const int rookDirs[4] = { 8, -8, 1, -1 };
    for (int dir : rookDirs) {
        int sq = targetSquare;
        while (true) {
            sq += dir;
            if (sq < 0 || sq >= 64 ||
                (sq % 8 == 0 && dir == 1) ||
                (sq % 8 == 7 && dir == -1)) break;

            Bitboard sqMask = 1ULL << sq;
            if ((attackerRooks | attackerQueens) & sqMask) return true;
            if (occupiedSquares & sqMask) break; // blocked
        }
    }

    // King attacks (adjacent squares)
    const int kingSteps[8] = { 8, -8, 1, -1, 9, 7, -9, -7 };
    for (int step : kingSteps) {
        int sq = targetSquare + step;
        if (sq >= 0 && sq < 64 && std::abs((targetSquare % 8) - (sq % 8)) <= 1) {
            if (attackerKing & (1ULL << sq)) return true;
        }
    }

    return false;
}

static inline void init_slider_pext_tables_once() {
    static bool done = false;
    if (!done) {
        init_slider_pext_tables();
        done = true;
    }
}

Bitboard Board::computePinnedMask(bool forWhite) const {
    const Bitboard ownPieces   = forWhite ? whitePieces : blackPieces;
    const Bitboard enemyPieces = forWhite ? blackPieces : whitePieces;
    const Bitboard occ = ownPieces | enemyPieces;

    const Bitboard kingBB = forWhite ? whiteKing : blackKing;
    if (!kingBB) return 0;

    const int kingSq = lsb_index(kingBB);

    const Bitboard enemyRookQ = forWhite ? (blackRooks   | blackQueens)
                                         : (whiteRooks   | whiteQueens);
    const Bitboard enemyBishQ = forWhite ? (blackBishops | blackQueens)
                                         : (whiteBishops | whiteQueens);

    Bitboard pinned = 0;

    for (int di = 0; di < 8; ++di) {
        const bool diag = (di == DIR_NE || di == DIR_NW || di == DIR_SE || di == DIR_SW);
        const Bitboard sliders = diag ? enemyBishQ : enemyRookQ;

        Bitboard ray1 = RAY[di][kingSq];
        Bitboard blockers1 = ray1 & occ;
        if (!blockers1) continue;

        const int b1Sq = first_blocker_sq(blockers1, DIR_DELTA[di]);
        const Bitboard b1Mask = 1ULL << b1Sq;
        if (!(ownPieces & b1Mask)) continue; // first blocker must be ours

        Bitboard ray2 = RAY[di][b1Sq];
        Bitboard blockers2 = ray2 & occ;
        if (!blockers2) continue;

        const int b2Sq = first_blocker_sq(blockers2, DIR_DELTA[di]);
        const Bitboard b2Mask = 1ULL << b2Sq;

        if (sliders & b2Mask) pinned |= b1Mask;
    }

    return pinned;
}

Board::Board() {
    init_attack_tables_once();
    init_slider_pext_tables_once();
    initializeZobristTable();
    createBoard();
    resize_tt(64);  // Calls the resize function to allocate memory
    clear_tt();     // Clear the table to reset all entries
    loadOpeningBook();
    maxHistoryValue = 0x0000000000000100ULL;
    //std::cout << "Number of entries in the transposition table: " << countTranspositionTableEntries() << std::endl;
}

void Board::createBoard() {
    whitePawns   = 0x000000000000FF00ULL;
    blackPawns   = 0x00FF000000000000ULL;
    whiteRooks   = 0x0000000000000081ULL;
    blackRooks   = 0x8100000000000000ULL;
    whiteKnights = 0x0000000000000042ULL;
    blackKnights = 0x4200000000000000ULL;
    whiteBishops = 0x0000000000000024ULL;
    blackBishops = 0x2400000000000000ULL;
    whiteQueens  = 0x0000000000000010ULL;
    blackQueens  = 0x1000000000000000ULL;
    whiteKing    = 0x0000000000000008ULL;
    blackKing    = 0x0800000000000000ULL;

    enPassantTarget = 0;
    epFile = -1;

    whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
    blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;

    whiteToMove = true;

    whiteKingMoved  = false;
    whiteLRookMoved = false;
    whiteRRookMoved = false;
    blackKingMoved  = false;
    blackLRookMoved = false;
    blackRRookMoved = false;

    rebuildMailbox();
    zobristHash = generateZobristHash();

    // (Optional) keep if you still use it outside search:
    positionHistory.clear();

    // --- NEW: repetition stack init ---
    repPly = 0;
    repIrrevIndex = 0;
    repStack[repPly++] = zobristHash;
}

void Board::createBoardFromFEN(const std::string& fen) {
    parseFEN(fen, *this);
    zobristHash = generateZobristHash();

    positionHistory.clear();

    repPly = 0;
    repIrrevIndex = 0;
    repStack[repPly++] = zobristHash;
}

void Board::printBoard() {
    auto getPieceChar = [this](int index) -> char {
        if ((whitePawns >> index) & 1) return 'P';
        if ((blackPawns >> index) & 1) return 'p';
        if ((whiteRooks >> index) & 1) return 'R';
        if ((blackRooks >> index) & 1) return 'r';
        if ((whiteKnights >> index) & 1) return 'N';
        if ((blackKnights >> index) & 1) return 'n';
        if ((whiteBishops >> index) & 1) return 'B';
        if ((blackBishops >> index) & 1) return 'b';
        if ((whiteQueens >> index) & 1) return 'Q';
        if ((blackQueens >> index) & 1) return 'q';
        if ((whiteKing >> index) & 1) return 'K';
        if ((blackKing >> index) & 1) return 'k';
        return '.';
        };

    std::stringstream fenStream;
    int emptyCount = 0;

    // Generate FEN for the board pieces and print the board
    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 7; file >= 0; --file) { // Flip the x-axis by iterating from right to left
            int index = rank * 8 + file;
            char piece = getPieceChar(index);
            if (piece == '.') {
                emptyCount++;
            }
            else {
                if (emptyCount != 0) {
                    fenStream << emptyCount;
                    emptyCount = 0;
                }
                fenStream << piece;
            }
            std::cout << piece << " "; // Print the piece on the console
        }
        if (emptyCount != 0) {
            fenStream << emptyCount;
            emptyCount = 0;
        }
        if (rank > 0) fenStream << '/';
        std::cout << "\n"; // New line after each rank
    }
    std::cout << std::endl;

    // Determine castling rights
    std::string castlingRights;
    if (!whiteKingMoved) {
        if (!whiteRRookMoved) castlingRights += 'K';
        if (!whiteLRookMoved) castlingRights += 'Q';
    }
    if (!blackKingMoved) {
        if (!blackRRookMoved) castlingRights += 'k';
        if (!blackLRookMoved) castlingRights += 'q';
    }
    if (castlingRights.empty()) castlingRights = "-";

    // En Passant target
    std::string enPassant = "-";
    if (enPassantTarget) {
        int epIndex = lsb_index(enPassantTarget);
        if (epIndex >= 16 && epIndex <= 55) {
            enPassant = numToBoardPosition(epIndex);
        }
    }

    // Determine who's move it is
    char playerToMove = whiteToMove ? 'w' : 'b';

    // Complete FEN string with game state
    fenStream << ' ' << playerToMove << ' ' << castlingRights << ' ' << enPassant << " 0 1";

    // Output FEN string
    std::cout << "FEN: " << fenStream.str() << std::endl;
}

std::string numToBoardPosition(int num) {
    // Ensure the number is within valid range
    if (num < 0 || num > 63) {
        return "Invalid num";
    }

    // Calculate the rank and file
    int rank = num / 8;
    int file = num % 8;

    // Convert rank and file to chess notation
    char rankChar = '1' + rank;
    char fileChar = 'h' - file;

    // Return the board position as a string
    return std::string(1, fileChar) + rankChar;
}

void Board::generatePawnMoves(MoveList& moves, Bitboard pawns, Bitboard ownPieces, Bitboard opponentPieces) {
    Bitboard emptySquares = ~(ownPieces | opponentPieces);
    Bitboard promotionRank = whiteToMove ? 0xFF00000000000000ULL : 0x00000000000000FFULL;

    // Single pawn moves
    Bitboard singlePush = whiteToMove ? (pawns << 8) & emptySquares : (pawns >> 8) & emptySquares;
    Bitboard singlePushMask = singlePush;
    while (singlePushMask) {
        int to = pop_lsb(singlePushMask);
        int from = whiteToMove ? to - 8 : to + 8;

        if ((1ULL << to) & promotionRank) {
            moves.push(Move(from, to, 'q'));
            moves.push(Move(from, to, 'r'));
            moves.push(Move(from, to, 'b'));
            moves.push(Move(from, to, 'n'));
        }
        else {
            moves.push(Move(from, to));
        }
    }

    // Double pawn moves (only from the starting position)
    Bitboard startRankMask = whiteToMove ? 0x000000000000FF00ULL : 0x00FF000000000000ULL;
    Bitboard doublePush = whiteToMove ? ((pawns & startRankMask) << 16) & (emptySquares << 8) & emptySquares
        : ((pawns & startRankMask) >> 16) & (emptySquares >> 8) & emptySquares;
    Bitboard doublePushMask = doublePush;
    while (doublePushMask) {
        int to = pop_lsb(doublePushMask);
        int from = whiteToMove ? to - 16 : to + 16;
        moves.push(Move(from, to));
    }

    // Pawn captures
    Bitboard leftCaptures = whiteToMove ? (pawns << 9) & opponentPieces & NOT_H_FILE
        : (pawns >> 9) & opponentPieces & NOT_A_FILE;
    Bitboard rightCaptures = whiteToMove ? (pawns << 7) & opponentPieces & NOT_A_FILE
        : (pawns >> 7) & opponentPieces & NOT_H_FILE;
    int from;
    while (leftCaptures) {
        int to = pop_lsb(leftCaptures);
        from = whiteToMove ? to - 9 : to + 9;

        Move move(from, to);
        move.isCapture = true;
        if ((1ULL << to) & promotionRank) {
            move.promotion = 'q';
            moves.push(Move(move));
            move.promotion = 'r';
            moves.push(Move(move));
            move.promotion = 'b';
            moves.push(Move(move));
            move.promotion = 'n';
            moves.push(Move(move));
        }
        else {
            moves.push(Move(move));
        }
    }

    while (rightCaptures) {
        int to = pop_lsb(rightCaptures);
        from = whiteToMove ? to - 7 : to + 7;

        Move move(from, to);
        move.isCapture = true;
        if ((1ULL << to) & promotionRank) {
            move.promotion = 'q';
            moves.push(move);
            move.promotion = 'r';
            moves.push(move);
            move.promotion = 'b';
            moves.push(move);
            move.promotion = 'n';
            moves.push(move);
        }
        else {
            moves.push(move);
        }
    }

    // en passant
    Bitboard leftPassant = whiteToMove ? (pawns << 9) & enPassantTarget & NOT_H_FILE
        : (pawns >> 9) & enPassantTarget & NOT_A_FILE;
    Bitboard rightPassant = whiteToMove ? (pawns << 7) & enPassantTarget & NOT_A_FILE
        : (pawns >> 7) & enPassantTarget & NOT_H_FILE;
   
    while (leftPassant) {
        int to = pop_lsb(leftPassant);
        from = whiteToMove ? to - 9 : to + 9;

        Move move(from, to);
        move.isCapture = true;
        moves.push(move);
    }

    while (rightPassant) {
        int to = pop_lsb(rightPassant);
        from = whiteToMove ? to - 7 : to + 7;

        Move move(from, to);
        move.isCapture = true;
        moves.push(move);
    } 
}

void Board::generateBishopMoves(MoveList& moves, Bitboard bishops, Bitboard ownPieces, Bitboard opponentPieces) {
    const Bitboard occupied = ownPieces | opponentPieces;

    while (bishops) {
        const int from = pop_lsb(bishops);

        Bitboard targets = bishop_attacks(from, occupied) & ~ownPieces;

        Bitboard captures = targets & opponentPieces;
        Bitboard quiets   = targets & ~opponentPieces;

        while (quiets) {
            int to = pop_lsb(quiets);
            moves.push(Move(from, to));
        }
        while (captures) {
            int to = pop_lsb(captures);
            Move m(from, to);
            m.isCapture = true;
            moves.push(m);
        }
    }
}

void Board::generateRookMoves(MoveList& moves, Bitboard rooks, Bitboard ownPieces, Bitboard opponentPieces) {
    const Bitboard occupied = ownPieces | opponentPieces;

    while (rooks) {
        const int from = pop_lsb(rooks);

        Bitboard targets = rook_attacks(from, occupied) & ~ownPieces;

        Bitboard captures = targets & opponentPieces;
        Bitboard quiets   = targets & ~opponentPieces;

        while (quiets) {
            int to = pop_lsb(quiets);
            moves.push(Move(from, to));
        }
        while (captures) {
            int to = pop_lsb(captures);
            Move m(from, to);
            m.isCapture = true;
            moves.push(m);
        }
    }
}

void Board::generateKnightMoves(MoveList& moves, Bitboard knights, Bitboard ownPieces, Bitboard opponentPieces) {
    while (knights) {
        const int from = pop_lsb(knights);

        Bitboard targets = KNIGHT_ATTACKS[from] & ~ownPieces;

        Bitboard captures = targets & opponentPieces;
        Bitboard quiets   = targets & ~opponentPieces;

        while (quiets) {
            int to = pop_lsb(quiets);
            moves.push(Move(from, to));
        }
        while (captures) {
            int to = pop_lsb(captures);
            Move m(from, to);
            m.isCapture = true;
            moves.push(m);
        }
    }
}

void Board::generateKingMoves(MoveList& moves, Bitboard kingBitboard, Bitboard ownPieces, Bitboard opponentPieces) {
    const int kingFromSquare = lsb_index(kingBitboard);
    const Bitboard kingFromMask = 1ULL << kingFromSquare;

    const Bitboard allOccupied = (ownPieces | opponentPieces);

    // Identify opponent piece sets (attackers)
    const bool attackersAreWhite = !whiteToMove;

    Bitboard attackerPawns   = attackersAreWhite ? whitePawns   : blackPawns;
    Bitboard attackerKnights = attackersAreWhite ? whiteKnights : blackKnights;
    Bitboard attackerBishops = attackersAreWhite ? whiteBishops : blackBishops;
    Bitboard attackerRooks   = attackersAreWhite ? whiteRooks   : blackRooks;
    Bitboard attackerQueens  = attackersAreWhite ? whiteQueens  : blackQueens;
    Bitboard attackerKing    = attackersAreWhite ? whiteKing    : blackKing;

    // Occupancy with our king removed (important: king moving can uncover slider attacks)
    const Bitboard occupiedWithoutOurKing = allOccupied & ~kingFromMask;

    // ---------------------------
    // King steps via precomputed table (no edge/mod/abs checks)
    // ---------------------------
    Bitboard kingTargets = KING_ATTACKS[kingFromSquare] & ~ownPieces;

    while (kingTargets) {
        const int kingToSquare = pop_lsb(kingTargets);
        const Bitboard kingToMask = 1ULL << kingToSquare;

        // If capturing, remove the captured piece from occupancy + from attacker sets
        Bitboard occupiedAfterKingMove = occupiedWithoutOurKing;

        Bitboard pawnsAfterCapture   = attackerPawns;
        Bitboard knightsAfterCapture = attackerKnights;
        Bitboard bishopsAfterCapture = attackerBishops;
        Bitboard rooksAfterCapture   = attackerRooks;
        Bitboard queensAfterCapture  = attackerQueens;
        Bitboard kingAfterCapture    = attackerKing;

        if (opponentPieces & kingToMask) {
            occupiedAfterKingMove &= ~kingToMask;

            pawnsAfterCapture   &= ~kingToMask;
            knightsAfterCapture &= ~kingToMask;
            bishopsAfterCapture &= ~kingToMask;
            rooksAfterCapture   &= ~kingToMask;
            queensAfterCapture  &= ~kingToMask;
            kingAfterCapture    &= ~kingToMask;
        }

        // King can't move into check
        if (isSquareAttacked_fast(
                kingToSquare,
                attackersAreWhite,
                occupiedAfterKingMove,
                pawnsAfterCapture,
                knightsAfterCapture,
                bishopsAfterCapture,
                rooksAfterCapture,
                queensAfterCapture,
                kingAfterCapture
            )) {
            continue;
        }

        Move m(kingFromSquare, kingToSquare);
        if (opponentPieces & kingToMask) m.isCapture = true;
        moves.push(m);
    }

    // ---------------------------
    // Castling (same logic as your existing function)
    // ---------------------------
    const bool kingCurrentlyInCheck =
        isSquareAttacked_fast(
            kingFromSquare,
            attackersAreWhite,
            allOccupied,
            attackerPawns,
            attackerKnights,
            attackerBishops,
            attackerRooks,
            attackerQueens,
            attackerKing
        );

    if (!kingCurrentlyInCheck) {
        if (whiteToMove) {
            // White kingside
            if (!whiteKingMoved && !whiteRRookMoved && (whiteRooks & 0x0000000000000001ULL)) {
                const Bitboard emptyBetweenMask = 0x0000000000000006ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked_fast(
                            kingFromSquare - 1,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        ) &&
                        !isSquareAttacked_fast(
                            kingFromSquare - 2,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        )) {
                        moves.push(Move(kingFromSquare, kingFromSquare - 2));
                    }
                }
            }

            // White queenside
            if (!whiteKingMoved && !whiteLRookMoved && (whiteRooks & 0x0000000000000080ULL)) {
                const Bitboard emptyBetweenMask = 0x0000000000000070ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked_fast(
                            kingFromSquare + 1,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        ) &&
                        !isSquareAttacked_fast(
                            kingFromSquare + 2,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        )) {
                        moves.push(Move(kingFromSquare, kingFromSquare + 2));
                    }
                }
            }
        } else {
            // Black kingside
            if (!blackKingMoved && !blackRRookMoved && (blackRooks & 0x0100000000000000ULL)) {
                const Bitboard emptyBetweenMask = 0x0600000000000000ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked_fast(
                            kingFromSquare - 1,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        ) &&
                        !isSquareAttacked_fast(
                            kingFromSquare - 2,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        )) {
                        moves.push(Move(kingFromSquare, kingFromSquare - 2));
                    }
                }
            }

            // Black queenside
            if (!blackKingMoved && !blackLRookMoved && (blackRooks & 0x8000000000000000ULL)) {
                const Bitboard emptyBetweenMask = 0x7000000000000000ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked_fast(
                            kingFromSquare + 1,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        ) &&
                        !isSquareAttacked_fast(
                            kingFromSquare + 2,
                            attackersAreWhite,
                            occupiedWithoutOurKing,
                            attackerPawns,
                            attackerKnights,
                            attackerBishops,
                            attackerRooks,
                            attackerQueens,
                            attackerKing
                        )) {
                        moves.push(Move(kingFromSquare, kingFromSquare + 2));
                    }
                }
            }
        }
    }
}

void Board::generateQueenMoves(MoveList& moves, Bitboard queens, Bitboard ownPieces, Bitboard opponentPieces) {
    generateBishopMoves(moves, queens, ownPieces, opponentPieces);
    generateRookMoves(moves, queens, ownPieces, opponentPieces);
}

void Board::generateAllMoves(MoveList& legalMoves) {
    legalMoves.clear();

    Bitboard ownPieces = whiteToMove ? whitePieces : blackPieces;
    Bitboard opponentPieces = whiteToMove ? blackPieces : whitePieces;
    
    MoveList allMoves;
    generatePawnMoves(allMoves, whiteToMove ? whitePawns : blackPawns,  ownPieces, opponentPieces);
    generateBishopMoves(allMoves, whiteToMove ? whiteBishops : blackBishops, ownPieces, opponentPieces);
    generateRookMoves(allMoves, whiteToMove ? whiteRooks : blackRooks, ownPieces, opponentPieces);
    generateKnightMoves(allMoves, whiteToMove ? whiteKnights : blackKnights, ownPieces, opponentPieces);
    generateQueenMoves(allMoves, whiteToMove ? whiteQueens : blackQueens, ownPieces, opponentPieces);

    MoveList kingMoves;
    generateKingMoves(kingMoves, whiteToMove ? whiteKing : blackKing, ownPieces, opponentPieces);

    const bool currentlyInCheck = amIInCheck(whiteToMove);
    const bool sideIsWhite = whiteToMove;
    const Bitboard pinnedMask = computePinnedMask(sideIsWhite);
    const Bitboard epStore = enPassantTarget;

    // Filter pseudo -> legalOut (your same logic, but no vectors)
    for (int i = 0; i < allMoves.size; ++i) {
        Move mv = allMoves.m[i];

        bool isEnPassantMove = false;
        if (epStore & (1ULL << mv.to)) {
            if (sideIsWhite) {
                if ((whitePawns & (1ULL << mv.from)) && (mv.to == mv.from + 7 || mv.to == mv.from + 9))
                    isEnPassantMove = true;
            } else {
                if ((blackPawns & (1ULL << mv.from)) && (mv.to == mv.from - 7 || mv.to == mv.from - 9))
                    isEnPassantMove = true;
            }
        }

        if (!currentlyInCheck && !isEnPassantMove && !(pinnedMask & (1ULL << mv.from))) {
            legalMoves.push(mv);
            continue;
        }

        Undo u;
        makeMove(mv, u);
        if (!amIInCheck(!whiteToMove)) {
            legalMoves.push(mv);
        }
        undoMove(mv, u);
    }

    // append king moves
    for (int i = 0; i < kingMoves.size; ++i) {
        legalMoves.push(kingMoves.m[i]);
    }
}

bool Board::amIInCheck(bool player) {
    const Bitboard ownKing = player ? whiteKing : blackKing;
    const int kingPos = lsb_index(ownKing);

    const bool attackersAreWhite = !player;

    const Bitboard occ = whitePieces | blackPieces;

    const Bitboard attackerPawns   = attackersAreWhite ? whitePawns   : blackPawns;
    const Bitboard attackerKnights = attackersAreWhite ? whiteKnights : blackKnights;
    const Bitboard attackerBishops = attackersAreWhite ? whiteBishops : blackBishops;
    const Bitboard attackerRooks   = attackersAreWhite ? whiteRooks   : blackRooks;
    const Bitboard attackerQueens  = attackersAreWhite ? whiteQueens  : blackQueens;
    const Bitboard attackerKing    = attackersAreWhite ? whiteKing    : blackKing;

    return isSquareAttacked_fast(
        kingPos,
        attackersAreWhite,
        occ,
        attackerPawns,
        attackerKnights,
        attackerBishops,
        attackerRooks,
        attackerQueens,
        attackerKing
    );
}

void Board::makeNullMove(Undo& u) {
    // save state
    u.prevHash = zobristHash;

    u.prevEnPassantTarget = enPassantTarget;
    u.prevEpFile = epFile;

    u.prevWhiteKingMoved  = whiteKingMoved;
    u.prevWhiteLRookMoved = whiteLRookMoved;
    u.prevWhiteRRookMoved = whiteRRookMoved;

    u.prevBlackKingMoved  = blackKingMoved;
    u.prevBlackLRookMoved = blackLRookMoved;
    u.prevBlackRRookMoved = blackRRookMoved;

    // remove old EP from hash then clear EP
    if (epFile != -1) {
        zobristHash ^= zobristEnPassant[epFile];
    }
    enPassantTarget = 0;
    epFile = -1;

    // toggle side
    whiteToMove = !whiteToMove;
    zobristHash ^= zobristSideToMove;
}

void Board::undoNullMove(const Undo& u) {
    // restore state exactly
    enPassantTarget = u.prevEnPassantTarget;
    epFile = u.prevEpFile;

    whiteKingMoved  = u.prevWhiteKingMoved;
    whiteLRookMoved = u.prevWhiteLRookMoved;
    whiteRRookMoved = u.prevWhiteRRookMoved;

    blackKingMoved  = u.prevBlackKingMoved;
    blackLRookMoved = u.prevBlackLRookMoved;
    blackRRookMoved = u.prevBlackRRookMoved;

    // restore side + hash in O(1)
    whiteToMove = !whiteToMove;
    zobristHash = u.prevHash;
}

void Board::makeMove(Move& move, Undo& u) {
    // ---- store undo state ----
    u.prevHash = zobristHash;

    u.prevEnPassantTarget = enPassantTarget;
    u.prevEpFile = epFile;

    u.prevWhiteKingMoved  = whiteKingMoved;
    u.prevWhiteLRookMoved = whiteLRookMoved;
    u.prevWhiteRRookMoved = whiteRRookMoved;

    u.prevBlackKingMoved  = blackKingMoved;
    u.prevBlackLRookMoved = blackLRookMoved;
    u.prevBlackRRookMoved = blackRRookMoved;

    u.prevRepIrrevIndex = repIrrevIndex;

    u.capturedPiece = 0;
    u.wasEnPassant  = false;

    // ---- mailbox undo init ----
    u.movedPieceChar = pieceAt[move.from];
    u.capturedPieceChar = ' ';
    u.capturedSquare = -1;

    // ---- remove old EP from hash (EP is for 1 ply only) ----
    if (epFile != -1) {
        zobristHash ^= zobristEnPassant[epFile];
    }
    epFile = -1;
    enPassantTarget = 0;

    const Bitboard fromMask = 1ULL << move.from;
    const Bitboard toMask   = 1ULL << move.to;

    // ----------------- DERIVE CAPTURE (robust against book/external moves) -----------------
    const Bitboard oppOcc = whiteToMove ? blackPieces : whitePieces; // occupancy of opponent

    const bool pawnMover = (u.movedPieceChar == 'p' || u.movedPieceChar == 'P');
    const bool isEpSquare = (u.prevEnPassantTarget & toMask) != 0;

    const int delta = move.to - move.from;
    const bool epDeltaOk = whiteToMove ? (delta == 7 || delta == 9) : (delta == -7 || delta == -9);

    const bool isEpCap   = pawnMover && isEpSquare && (pieceAt[move.to] == ' ') && epDeltaOk;
    const bool isNormCap = (oppOcc & toMask) != 0;

    move.isCapture = (isEpCap || isNormCap);

    // ---- mailbox capture removal (before we overwrite destination) ----
    if (move.isCapture) {
        if (isEpCap) {
            const int victimSq = whiteToMove ? (move.to - 8) : (move.to + 8);
            u.wasEnPassant = true;
            u.capturedSquare = victimSq;
            u.capturedPieceChar = pieceAt[victimSq];
            pieceAt[victimSq] = ' ';
        } else {
            u.capturedSquare = move.to;
            u.capturedPieceChar = pieceAt[move.to];
            pieceAt[move.to] = ' ';
        }
    }

    // ---- mailbox move piece from->to (promotion handled) ----
    pieceAt[move.from] = ' ';

    char placed = u.movedPieceChar;
    if (move.promotion) {
        placed = whiteToMove
            ? move.promotion
            : (char)std::toupper((unsigned char)move.promotion);
    }
    pieceAt[move.to] = placed;

    // ---------------- BITBOARD/ZOBRIST MOVE (SWITCH ON MAILBOX CHAR) ----------------
    if (whiteToMove) {
        // WHITE pieces are lowercase in your mailbox
        switch (u.movedPieceChar) {
            case 'p': {
                zobristHash ^= zobristTable[getPieceIndex('p')][move.from];

                if (move.to == move.from + 16) {
                    const int epSq = move.from + 8;
                    enPassantTarget = 1ULL << epSq;
                    epFile = (epSq & 7);
                }

                if (move.promotion) {
                    zobristHash ^= zobristTable[getPieceIndex(move.promotion)][move.to];
                    whitePawns ^= fromMask;
                    switch (move.promotion) {
                        case 'q': whiteQueens  |= toMask; break;
                        case 'r': whiteRooks   |= toMask; break;
                        case 'b': whiteBishops |= toMask; break;
                        case 'n': whiteKnights |= toMask; break;
                    }
                } else {
                    zobristHash ^= zobristTable[getPieceIndex('p')][move.to];
                    whitePawns ^= fromMask | toMask;
                }
                break;
            }
            case 'r': {
                zobristHash ^= zobristTable[getPieceIndex('r')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('r')][move.to];
                whiteRooks ^= fromMask | toMask;
                if (move.from == 0) whiteRRookMoved = true;
                if (move.from == 7) whiteLRookMoved = true;
                break;
            }
            case 'n': {
                zobristHash ^= zobristTable[getPieceIndex('n')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('n')][move.to];
                whiteKnights ^= fromMask | toMask;
                break;
            }
            case 'b': {
                zobristHash ^= zobristTable[getPieceIndex('b')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('b')][move.to];
                whiteBishops ^= fromMask | toMask;
                break;
            }
            case 'q': {
                zobristHash ^= zobristTable[getPieceIndex('q')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('q')][move.to];
                whiteQueens ^= fromMask | toMask;
                break;
            }
            case 'k': {
                zobristHash ^= zobristTable[getPieceIndex('k')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('k')][move.to];

                whiteKing ^= fromMask | toMask;
                whiteKingMoved = true;

                // castling rook shift (keep your existing)
                if (move.to == move.from - 2) {
                    zobristHash ^= zobristTable[getPieceIndex('r')][0];
                    zobristHash ^= zobristTable[getPieceIndex('r')][2];
                    whiteRooks ^= 0x0000000000000005ULL;

                    pieceAt[2] = pieceAt[0];
                    pieceAt[0] = ' ';
                } else if (move.to == move.from + 2) {
                    zobristHash ^= zobristTable[getPieceIndex('r')][7];
                    zobristHash ^= zobristTable[getPieceIndex('r')][4];
                    whiteRooks ^= 0x0000000000000090ULL;

                    pieceAt[4] = pieceAt[7];
                    pieceAt[7] = ' ';
                }
                break;
            }
            default:
                // mailbox + bitboards desynced
                break;
        }

        // ---------------- CAPTURE RESOLUTION USING MAILBOX CAPTURE CHAR ----------------
        if (move.isCapture) {
            if (u.wasEnPassant) {
                const int victimSq = u.capturedSquare;
                const Bitboard vMask = 1ULL << victimSq;

                // victim is a BLACK pawn in your mailbox => 'P'
                zobristHash ^= zobristTable[getPieceIndex('P')][victimSq];
                blackPawns &= ~vMask;
                u.capturedPiece = 'p';
            } else {
                const char capChar = u.capturedPieceChar; // black piece => 'P','N','B','R','Q','K'
                if (capChar != ' ') {
                    zobristHash ^= zobristTable[getPieceIndex(capChar)][move.to];
                    u.capturedPiece = pieceTypeLower(capChar);

                    switch (capChar) {
                        case 'P': blackPawns   &= ~toMask; break;
                        case 'R': blackRooks   &= ~toMask; break;
                        case 'N': blackKnights &= ~toMask; break;
                        case 'B': blackBishops &= ~toMask; break;
                        case 'Q': blackQueens  &= ~toMask; break;
                        case 'K': blackKing    &= ~toMask; break;
                    }

                    // captured rook on original square clears castling rights
                    if (capChar == 'R') {
                        if (move.to == 56) blackRRookMoved = true;
                        if (move.to == 63) blackLRookMoved = true;
                    }
                }
            }
        }
    } else {
        // BLACK pieces are uppercase in your mailbox
        switch (u.movedPieceChar) {
            case 'P': {
                zobristHash ^= zobristTable[getPieceIndex('P')][move.from];

                if (move.to == move.from - 16) {
                    const int epSq = move.from - 8;
                    enPassantTarget = 1ULL << epSq;
                    epFile = (epSq & 7);
                }
                if (move.promotion) {
                    const char promo = (char)std::toupper((unsigned char)move.promotion);
                    zobristHash ^= zobristTable[getPieceIndex(promo)][move.to];

                    blackPawns ^= fromMask;
                    switch (move.promotion) {
                        case 'q': blackQueens  |= toMask; break;
                        case 'r': blackRooks   |= toMask; break;
                        case 'b': blackBishops |= toMask; break;
                        case 'n': blackKnights |= toMask; break;
                    }
                } else {
                    zobristHash ^= zobristTable[getPieceIndex('P')][move.to];
                    blackPawns ^= fromMask | toMask;
                }
                break;
            }
            case 'R': {
                zobristHash ^= zobristTable[getPieceIndex('R')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('R')][move.to];
                blackRooks ^= fromMask | toMask;
                if (move.from == 56) blackRRookMoved = true;
                if (move.from == 63) blackLRookMoved = true;
                break;
            }
            case 'N': {
                zobristHash ^= zobristTable[getPieceIndex('N')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('N')][move.to];
                blackKnights ^= fromMask | toMask;
                break;
            }
            case 'B': {
                zobristHash ^= zobristTable[getPieceIndex('B')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('B')][move.to];
                blackBishops ^= fromMask | toMask;
                break;
            }
            case 'Q': {
                zobristHash ^= zobristTable[getPieceIndex('Q')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('Q')][move.to];
                blackQueens ^= fromMask | toMask;
                break;
            }
            case 'K': {
                zobristHash ^= zobristTable[getPieceIndex('K')][move.from];
                zobristHash ^= zobristTable[getPieceIndex('K')][move.to];

                blackKing ^= fromMask | toMask;
                blackKingMoved = true;

                if (move.to == move.from - 2) {
                    zobristHash ^= zobristTable[getPieceIndex('R')][56];
                    zobristHash ^= zobristTable[getPieceIndex('R')][58];
                    blackRooks ^= 0x0500000000000000ULL;

                    pieceAt[58] = pieceAt[56];
                    pieceAt[56] = ' ';
                } else if (move.to == move.from + 2) {
                    zobristHash ^= zobristTable[getPieceIndex('R')][63];
                    zobristHash ^= zobristTable[getPieceIndex('R')][60];
                    blackRooks ^= 0x9000000000000000ULL;

                    pieceAt[60] = pieceAt[63];
                    pieceAt[63] = ' ';
                }
                break;
            }
            default:
                break;
        }

        if (move.isCapture) {
            if (u.wasEnPassant) {
                const int victimSq = u.capturedSquare;
                const Bitboard vMask = 1ULL << victimSq;

                // victim is a WHITE pawn in your mailbox => 'p'
                zobristHash ^= zobristTable[getPieceIndex('p')][victimSq];
                whitePawns &= ~vMask;
                u.capturedPiece = 'p';
            } else {
                const char capChar = u.capturedPieceChar; // white piece => 'p','n','b','r','q','k'
                if (capChar != ' ') {
                    zobristHash ^= zobristTable[getPieceIndex(capChar)][move.to];
                    u.capturedPiece = pieceTypeLower(capChar);

                    switch (capChar) {
                        case 'p': whitePawns   &= ~toMask; break;
                        case 'r': whiteRooks   &= ~toMask; break;
                        case 'n': whiteKnights &= ~toMask; break;
                        case 'b': whiteBishops &= ~toMask; break;
                        case 'q': whiteQueens  &= ~toMask; break;
                        case 'k': whiteKing    &= ~toMask; break;
                    }

                    // captured rook on original square clears castling rights
                    if (capChar == 'r') {
                        if (move.to == 0) whiteRRookMoved = true;
                        if (move.to == 7) whiteLRookMoved = true;
                    }
                }
            }
        }
    }

    // ---- add new EP to hash ----
    if (epFile != -1) {
        zobristHash ^= zobristEnPassant[epFile];
    }

    // ---- update castling-right hash ----
    if (u.prevWhiteKingMoved  != whiteKingMoved)  zobristHash ^= zobristCastling[0];
    if (u.prevWhiteRRookMoved != whiteRRookMoved) zobristHash ^= zobristCastling[1];
    if (u.prevWhiteLRookMoved != whiteLRookMoved) zobristHash ^= zobristCastling[2];

    if (u.prevBlackKingMoved  != blackKingMoved)  zobristHash ^= zobristCastling[3];
    if (u.prevBlackRRookMoved != blackRRookMoved) zobristHash ^= zobristCastling[4];
    if (u.prevBlackLRookMoved != blackLRookMoved) zobristHash ^= zobristCastling[5];

    // ------------------------------------------------------------------
    // NEW: Incremental occupancy update (replaces OR-rebuild every move)
    // ------------------------------------------------------------------
    {
        Bitboard& moverOcc2 = whiteToMove ? whitePieces : blackPieces;
        Bitboard& oppOcc2   = whiteToMove ? blackPieces : whitePieces;

        // mover piece from -> to
        moverOcc2 ^= fromMask;
        moverOcc2 |= toMask;

        // capture removal (EP uses victim square, not 'to')
        if (move.isCapture) {
            const Bitboard capMask = u.wasEnPassant ? (1ULL << u.capturedSquare) : toMask;
            oppOcc2 &= ~capMask;
        }

        // castling rook squares (your engine mapping)
        const char kingChar = whiteToMove ? 'k' : 'K';
        if (u.movedPieceChar == kingChar) {
            if (move.to == move.from - 2) {
                // rook: 0->2 (white), 56->58 (black)
                const int rookFrom = whiteToMove ? 0 : 56;
                const int rookTo   = whiteToMove ? 2 : 58;
                moverOcc2 ^= (1ULL << rookFrom);
                moverOcc2 |= (1ULL << rookTo);
            } else if (move.to == move.from + 2) {
                // rook: 7->4 (white), 63->60 (black)
                const int rookFrom = whiteToMove ? 7 : 63;
                const int rookTo   = whiteToMove ? 4 : 60;
                moverOcc2 ^= (1ULL << rookFrom);
                moverOcc2 |= (1ULL << rookTo);
            }
        }
    }

    // toggle side
    whiteToMove = !whiteToMove;
    zobristHash ^= zobristSideToMove;

    // ---------------- repetition stack push + irreversible boundary ----------------
    const bool movedPawn2 = pawnMover;
    const bool castlingRightsChanged =
        (u.prevWhiteKingMoved  != whiteKingMoved)  ||
        (u.prevWhiteRRookMoved != whiteRRookMoved) ||
        (u.prevWhiteLRookMoved != whiteLRookMoved) ||
        (u.prevBlackKingMoved  != blackKingMoved)  ||
        (u.prevBlackRRookMoved != blackRRookMoved) ||
        (u.prevBlackLRookMoved != blackLRookMoved);

    const bool irreversible = move.isCapture || movedPawn2 || move.promotion || castlingRightsChanged;

    repStack[repPly++] = zobristHash;
    if (irreversible) repIrrevIndex = repPly - 1;
}

void Board::undoMove(const Move& move, const Undo& u) {
    enPassantTarget = u.prevEnPassantTarget;
    epFile = u.prevEpFile;

    whiteKingMoved  = u.prevWhiteKingMoved;
    whiteLRookMoved = u.prevWhiteLRookMoved;
    whiteRRookMoved = u.prevWhiteRRookMoved;

    blackKingMoved  = u.prevBlackKingMoved;
    blackLRookMoved = u.prevBlackLRookMoved;
    blackRRookMoved = u.prevBlackRRookMoved;

    const Bitboard fromMask = 1ULL << move.from;
    const Bitboard toMask   = 1ULL << move.to;

    // who made the move we're undoing?
    // (your existing logic relies on current whiteToMove being "side to play after the move")
    const bool undoingWhiteMove = !whiteToMove;

    // ---------------- EXISTING PIECE-BITBOARD UNDO (UNCHANGED LOGIC) ----------------
    if (undoingWhiteMove) {
        // undo WHITE move
        if (move.promotion) {
            whitePawns |= fromMask;
            switch (move.promotion) {
                case 'q': whiteQueens  &= ~toMask; break;
                case 'r': whiteRooks   &= ~toMask; break;
                case 'b': whiteBishops &= ~toMask; break;
                case 'n': whiteKnights &= ~toMask; break;
            }
        }
        else if (whitePawns   & toMask) whitePawns   ^= fromMask | toMask;
        else if (whiteRooks   & toMask) whiteRooks   ^= fromMask | toMask;
        else if (whiteKnights & toMask) whiteKnights ^= fromMask | toMask;
        else if (whiteBishops & toMask) whiteBishops ^= fromMask | toMask;
        else if (whiteQueens  & toMask) whiteQueens  ^= fromMask | toMask;
        else if (whiteKing    & toMask) {
            whiteKing ^= fromMask | toMask;
            if (move.to == move.from - 2)      whiteRooks ^= 0x0000000000000005ULL;
            else if (move.to == move.from + 2) whiteRooks ^= 0x0000000000000090ULL;
        }

        if (move.isCapture) {
            if (u.wasEnPassant) {
                blackPawns |= (toMask >> 8);
            } else {
                switch (u.capturedPiece) {
                    case 'p': blackPawns   |= toMask; break;
                    case 'r': blackRooks   |= toMask; break;
                    case 'n': blackKnights |= toMask; break;
                    case 'b': blackBishops |= toMask; break;
                    case 'q': blackQueens  |= toMask; break;
                    case 'k': blackKing    |= toMask; break;
                }
            }
        }
    }
    else {
        // undo BLACK move
        if (move.promotion) {
            blackPawns |= fromMask;
            switch (move.promotion) {
                case 'q': blackQueens  &= ~toMask; break;
                case 'r': blackRooks   &= ~toMask; break;
                case 'b': blackBishops &= ~toMask; break;
                case 'n': blackKnights &= ~toMask; break;
            }
        }
        else if (blackPawns   & toMask) blackPawns   ^= fromMask | toMask;
        else if (blackRooks   & toMask) blackRooks   ^= fromMask | toMask;
        else if (blackKnights & toMask) blackKnights ^= fromMask | toMask;
        else if (blackBishops & toMask) blackBishops ^= fromMask | toMask;
        else if (blackQueens  & toMask) blackQueens  ^= fromMask | toMask;
        else if (blackKing    & toMask) {
            blackKing ^= fromMask | toMask;
            if (move.to == move.from - 2)      blackRooks ^= 0x0500000000000000ULL;
            else if (move.to == move.from + 2) blackRooks ^= 0x9000000000000000ULL;
        }

        if (move.isCapture) {
            if (u.wasEnPassant) {
                whitePawns |= (toMask << 8);
            } else {
                switch (u.capturedPiece) {
                    case 'p': whitePawns   |= toMask; break;
                    case 'r': whiteRooks   |= toMask; break;
                    case 'n': whiteKnights |= toMask; break;
                    case 'b': whiteBishops |= toMask; break;
                    case 'q': whiteQueens  |= toMask; break;
                    case 'k': whiteKing    |= toMask; break;
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // NEW: Incremental occupancy restore (replaces OR-rebuild every undo)
    // ------------------------------------------------------------------
    {
        Bitboard& moverOcc = undoingWhiteMove ? whitePieces : blackPieces;
        Bitboard& oppOcc   = undoingWhiteMove ? blackPieces : whitePieces;

        // mover piece back: to -> from
        moverOcc &= ~toMask;
        moverOcc |= fromMask;

        // undo castling rook squares (your engine mapping)
        const char kingChar = undoingWhiteMove ? 'k' : 'K';
        if (u.movedPieceChar == kingChar) {
            if (move.to == move.from - 2) {
                // rook: 2->0 (white), 58->56 (black)
                const int rookFrom = undoingWhiteMove ? 2 : 58;
                const int rookTo   = undoingWhiteMove ? 0 : 56;
                moverOcc &= ~(1ULL << rookFrom);
                moverOcc |=  (1ULL << rookTo);
            } else if (move.to == move.from + 2) {
                // rook: 4->7 (white), 60->63 (black)
                const int rookFrom = undoingWhiteMove ? 4 : 60;
                const int rookTo   = undoingWhiteMove ? 7 : 63;
                moverOcc &= ~(1ULL << rookFrom);
                moverOcc |=  (1ULL << rookTo);
            }
        }

        // restore captured piece occupancy (EP uses victim square)
        if (move.isCapture) {
            const Bitboard capMask = u.wasEnPassant ? (1ULL << u.capturedSquare) : toMask;
            oppOcc |= capMask;
        }
    }

    // ---------------- MAILBOX UNDO ----------------
    const bool moverWasWhite = undoingWhiteMove;

    pieceAt[move.to] = ' ';
    pieceAt[move.from] = u.movedPieceChar;

    if (u.capturedSquare != -1) {
        pieceAt[u.capturedSquare] = u.capturedPieceChar;
    }

    const char kingChar2 = moverWasWhite ? 'k' : 'K';
    if (u.movedPieceChar == kingChar2) {
        if (move.to == move.from - 2) {
            int rookFrom = moverWasWhite ? 2 : 58;
            int rookTo   = moverWasWhite ? 0 : 56;
            pieceAt[rookTo] = pieceAt[rookFrom];
            pieceAt[rookFrom] = ' ';
        } else if (move.to == move.from + 2) {
            int rookFrom = moverWasWhite ? 4 : 60;
            int rookTo   = moverWasWhite ? 7 : 63;
            pieceAt[rookTo] = pieceAt[rookFrom];
            pieceAt[rookFrom] = ' ';
        }
    }

    // restore side to move
    whiteToMove = !whiteToMove;

    // Perfect O(1) restore
    zobristHash = u.prevHash;

    // ---------------- repetition stack pop + restore boundary ----------------
    if (repPly > 0) --repPly;
    repIrrevIndex = u.prevRepIrrevIndex;
}

char Board::getPieceAt(int index) const {
    return ((unsigned)index < 64) ? pieceAt[index] : ' ';
}

void setBit(Bitboard& bitboard, int square) {
    bitboard |= 1ULL << square;
}

void parseFEN(const std::string& fen, Board& board) {
    std::istringstream iss(fen);
    std::string boardStr, activeColor, castling, enPassant, halfmove, fullmove;

    // Read the components of the FEN string
    iss >> boardStr >> activeColor >> castling >> enPassant >> halfmove >> fullmove;

    // Reset all bitboards
    board.whitePawns = board.blackPawns = 0;
    board.whiteRooks = board.blackRooks = 0;
    board.whiteKnights = board.blackKnights = 0;
    board.whiteBishops = board.blackBishops = 0;
    board.whiteQueens = board.blackQueens = 0;
    board.whiteKing = board.blackKing = 0;

    // Reset castling rights
    board.whiteKingMoved = false;
    board.whiteLRookMoved = true;
    board.whiteRRookMoved = true;
    board.blackKingMoved = false;
    board.blackLRookMoved = true;
    board.blackRRookMoved = true;

    for (char a : castling) {
        if (a == 'K') {
            board.whiteRRookMoved = false;
        }
        else if (a == 'k') {
            board.blackRRookMoved = false;
        }
        else if (a == 'Q') {
            board.whiteLRookMoved = false;
        }
        else if (a == 'q') {
            board.blackLRookMoved = false;
        }
    }

    // Parse the board string
    int square = 63;
    for (char c : boardStr) {
        if (c == '/') {
            continue;
        }
        if (isdigit((unsigned char)c)) {
            square -= (c - '0');
        }
        else {
            switch (c) {
            case 'P': setBit(board.whitePawns, square); break;
            case 'R': setBit(board.whiteRooks, square); break;
            case 'N': setBit(board.whiteKnights, square); break;
            case 'B': setBit(board.whiteBishops, square); break;
            case 'Q': setBit(board.whiteQueens, square); break;
            case 'K': setBit(board.whiteKing, square); break;
            case 'p': setBit(board.blackPawns, square); break;
            case 'r': setBit(board.blackRooks, square); break;
            case 'n': setBit(board.blackKnights, square); break;
            case 'b': setBit(board.blackBishops, square); break;
            case 'q': setBit(board.blackQueens, square); break;
            case 'k': setBit(board.blackKing, square); break;
            }
            square--;
        }
    }

    // Set the active color
    board.whiteToMove = (activeColor == "w");

    board.enPassantTarget = 0;
    board.epFile = -1;

    if (enPassant != "-") {
        const int epSq = boardPositionToIndex(enPassant);
        if (epSq >= 0 && epSq < 64) {
            board.enPassantTarget = 1ULL << epSq;
            board.epFile = (epSq & 7);
        }
    }

    // Update the overall piece bitboards
    board.whitePieces = board.whitePawns | board.whiteRooks | board.whiteKnights |
        board.whiteBishops | board.whiteQueens | board.whiteKing;
    board.blackPieces = board.blackPawns | board.blackRooks | board.blackKnights |
        board.blackBishops | board.blackQueens | board.blackKing;

    board.rebuildMailbox();
}

void Board::rebuildMailbox() {
    pieceAt.fill(' ');

    auto place = [&](Bitboard bb, char c) {
        while (bb) {
            int sq = pop_lsb(bb);
            pieceAt[sq] = c;
        }
    };

    // white (lowercase in your engine)
    place(whitePawns, 'p');
    place(whiteKnights, 'n');
    place(whiteBishops, 'b');
    place(whiteRooks, 'r');
    place(whiteQueens, 'q');
    place(whiteKing,  'k');

    // black (uppercase in your engine)
    place(blackPawns, 'P');
    place(blackKnights, 'N');
    place(blackBishops, 'B');
    place(blackRooks, 'R');
    place(blackQueens, 'Q');
    place(blackKing, 'K');
}

int Board::getEnPassantFile() const {
    return epFile;
}

static const std::array<int8_t, 128> PIECE_IDX = []{
    std::array<int8_t, 128> t{};
    // fill with -1 without requiring constexpr support
    for (auto &x : t) x = -1;

    t['p']=0; t['n']=1; t['b']=2; t['r']=3; t['q']=4; t['k']=5;
    t['P']=6; t['N']=7; t['B']=8; t['R']=9; t['Q']=10; t['K']=11;
    return t;
}();

static inline __forceinline int piece_index(char c) {
    return PIECE_IDX[(unsigned char)c];
}

// If you want to keep the old name:
int Board::getPieceIndex(char piece) const {
    return piece_index(piece);
}


uint64_t Board::generateZobristHash() const {
    uint64_t hash = 0;

    // Add pieces to the hash
    for (int square = 0; square < NUM_SQUARES; ++square) {
        char piece = getPieceAt(square);
        if (piece != ' ') {
            int pieceIndex = getPieceIndex(piece); // A function to map piece character to an index
            hash ^= zobristTable[pieceIndex][square];
        }
    }

    // Add castling rights to the hash
    if (whiteKingMoved == false) hash ^= zobristCastling[0];
    if (whiteRRookMoved == false) hash ^= zobristCastling[1]; // White kingside castling right
    if (whiteLRookMoved == false) hash ^= zobristCastling[2]; // White queenside castling right

    if (blackKingMoved == false) hash ^= zobristCastling[3];
    if (blackRRookMoved == false) hash ^= zobristCastling[4]; // Black kingside castling right
    if (blackLRookMoved == false) hash ^= zobristCastling[5]; // Black queenside castling right

    // Add en passant square to the hash
    if (epFile != -1) {
        hash ^= zobristEnPassant[epFile];
    }

    // Add side to move to the hash
    if (whiteToMove) {
        hash ^= zobristSideToMove;
    }

    return hash;
}

void Board::updatePositionHistory(bool plus) {
    const uint64_t hash = zobristHash;

    if (plus){
        positionHistory[hash] += 1;
    }
    else {
        auto it = positionHistory.find(hash);
        if (it != positionHistory.end()) {
            it->second -= 1;
            if (it->second <= 0) positionHistory.erase(it);
        }
    }
}

bool Board::isThreefoldRepetition() {
    const uint64_t h = zobristHash;
    int count = 0;

    // scan only since last irreversible move; step by 2 to check same side-to-move positions
    for (int i = repPly - 1; i >= repIrrevIndex; i -= 2) {
        if (repStack[i] == h) {
            if (++count >= 3) return true;
        }
    }
    return false;
}

bool Board::isThreefoldRepetition(uint64_t hash) {
    int count = 0;

    // scan only since last irreversible move; step by 2 to check same side-to-move positions
    for (int i = repPly - 1; i >= repIrrevIndex; i -= 2) {
        if (repStack[i] == hash) {
            if (++count >= 3) return true;
        }
    }
    return false; 
}

bool isTacticalPosition(const std::vector<Move>& moves, const Board& board) {
    for (const Move& move : moves) {
        if (isGoodCapture(move, board) || isEqualCapture(move, board) || move.promotion) {
            return true;
        }
    }
    return false;
}

int getPieceValue(char piece) {
    switch (piece) {
    case 'P':
    case 'p':
        return 1;
    case 'N':
    case 'n':
        return 3;
    case 'B':
    case 'b':
        return 4;
    case 'R':
    case 'r':
        return 5;
    case 'Q':
    case 'q':
        return 9;
    case 'K':
    case 'k':
        return 0; // The king is invaluable
    default:
        return 0; // Empty square or unknown piece
    }
}

int Board::posToValue(int from) {
    return getPieceIndex(getPieceAt(from));
}

int isGoodCapture(const Move& move, const Board& board) {
    if (!move.isCapture) return 0;

    char attackerPiece = board.getPieceAt(move.from);

    // Default victim is whatever is on the destination square
    char victimPiece = board.getPieceAt(move.to);

    // If destination square is empty but capture flag is set,
    if (victimPiece == ' ') {
        // Determine EP victim square from side to move and your coordinate system.
        // In your engine: white pawn push = +8, so white EP captures remove pawn at to-8.
        int victimSq = board.whiteToMove ? (move.to - 8) : (move.to + 8);
        victimPiece = board.getPieceAt(victimSq);
    }

    int attackerValue = getPieceValue(attackerPiece);
    int victimValue   = getPieceValue(victimPiece);
    return victimValue - attackerValue;
}

bool isEqualCapture(const Move& move, const Board& board) {
    if (!move.isCapture) return false;

    char attackerPiece = board.getPieceAt(move.from);
    char victimPiece = board.getPieceAt(move.to);

    if (victimPiece == ' ') {
        int victimSq = board.whiteToMove ? (move.to - 8) : (move.to + 8);
        victimPiece = board.getPieceAt(victimSq);
    }

    return getPieceValue(attackerPiece) == getPieceValue(victimPiece);
}

bool isKillerMove(const Move& move, const Board& board, int depth) {
    return (move == board.killerMoves[0][depth] || move == board.killerMoves[1][depth]);
 }

void Board::resize_tt(uint64_t mb) {
    size_t entries = (mb * 1048576ull) / sizeof(TT_Entry);
    size_t new_entries = 1ull << (int)std::log2((double)entries);
    transposition_table.resize(new_entries);
    clear_tt();  // Clear the table to ensure all entries are reset after resizing
}

void Board::configureTranspositionTableSize(uint64_t sizeInMB) {
    resize_tt(sizeInMB);
}

void Board::record_tt_entry(uint64_t hash_key, int score, TTFlag flag, Move move, int depth) {
    TT_Entry& tt_entry = transposition_table[hash_key & (transposition_table.size() - 1)];

    // Use deeper or exact information to replace existing entry
    if (tt_entry.key != hash_key || depth > tt_entry.depth || flag == HASH_FLAG_EXACT) {
        tt_entry.key = hash_key;
        tt_entry.score = score;
        tt_entry.flag = flag;
        tt_entry.move = move;
        tt_entry.depth = depth;
    }
}

short Board::probe_tt_entry(uint64_t hash_key, int alpha, int beta, int depth, TT_Entry& return_entry) {
    TT_Entry& tt_entry = transposition_table[hash_key & (transposition_table.size() - 1)];

    if (tt_entry.key == hash_key) {
        return_entry = tt_entry;  // Copy the found entry to return_entry

        if (tt_entry.depth >= depth) {
            if (tt_entry.flag == HASH_FLAG_EXACT) return RETURN_HASH_SCORE;
            if (tt_entry.flag == HASH_FLAG_LOWER && tt_entry.score >= beta) return RETURN_HASH_SCORE;
            if (tt_entry.flag == HASH_FLAG_UPPER && tt_entry.score <= alpha) return RETURN_HASH_SCORE;
        }

        return USE_HASH_MOVE;
    }
    return NO_HASH_ENTRY;
}

void Board::clear_tt() {
    for (auto& tt_entry : transposition_table) {
        tt_entry = TT_Entry();  // Reset each entry
    }
}

TT_Entry* Board::probeTranspositionTable(uint64_t hash) {
    if (transposition_table.empty()) return nullptr;
    return &transposition_table[hash & (transposition_table.size() - 1)];
}


// Checking if it is a quiet position or not
bool isNullViable(Board& board) {
    return board.whiteToMove ? 
    ((std::popcount(board.whiteBishops) + std::popcount(board.whiteKnights) + (std::popcount(board.whiteRooks) * 2) + (std::popcount(board.whiteQueens) * 2)) >= 2) : 
    ((std::popcount(board.blackBishops) + std::popcount(board.blackKnights) + (std::popcount(board.blackRooks) * 2) + (std::popcount(board.blackQueens) * 2)) >= 2);
}

// Function to deserialize a Move object
std::istream& operator>>(std::istream& is, Move& move) {
    is.read(reinterpret_cast<char*>(&move.from), sizeof(move.from));
    is.read(reinterpret_cast<char*>(&move.to), sizeof(move.to));
    is.read(reinterpret_cast<char*>(&move.promotion), sizeof(move.promotion));
    return is;
}

// Function to deserialize a TT_Entry object
std::istream& operator>>(std::istream& is, TT_Entry& entry) {
    is.read(reinterpret_cast<char*>(&entry.key), sizeof(entry.key));
    is.read(reinterpret_cast<char*>(&entry.score), sizeof(entry.score));
    is.read(reinterpret_cast<char*>(&entry.depth), sizeof(entry.depth));
    is.read(reinterpret_cast<char*>(&entry.flag), sizeof(entry.flag));
    is >> entry.move;
    return is;
}

void Board::loadOpeningBook() {
    std::string filename = "transposition_table.dat";
    std::ifstream file2(filename, std::ios::binary);
    if (!file2) {
        std::cerr << "Failed to open file for reading: " << filename << std::endl;
        return;
    }
    TT_Entry entry2;
    while (file2 >> entry2) {
        if (entry2.key != 0 && entry2.move.from != -1 && entry2.move.to != -1) {
            record_tt_entry(entry2.key, 0, HASH_BOOK, entry2.move, 0);
        }
    }
}

// Function to convert a board position (e.g. "e2") to an index
int boardPositionToIndex(const std::string& pos) {
    if (pos.size() != 2) return -1; // Invalid position
    char fileChar = pos[0];
    char rankChar = pos[1];

    int file = 'h' - fileChar;
    int rank = rankChar - '1';

    if (file < 0 || file > 7 || rank < 0 || rank > 7) return -1; // Invalid position
    return rank * 8 + file;
}

// Function to convert move string (e.g., "e2e4") to a Move object
Move convertToMoveObject(const std::string& moveStr) {
    if (moveStr.size() != 4) return NO_MOVE; // Invalid move string
    std::string fromStr = moveStr.substr(0, 2);
    std::string toStr = moveStr.substr(2, 2);

    int from = boardPositionToIndex(fromStr);
    int to = boardPositionToIndex(toStr);

    return Move(from, to);
}

size_t Board::countTranspositionTableEntries() const {
    size_t count = 0;
    for (const auto& entry : transposition_table) {
        if (entry.key != 0) {
            ++count;
        }
    }
    return count;
}

int clamp(int value, int max, int min) {
    if (value > max) return max;
    if (value < min) return min;
    return value;
}

void Board::updateHistory(int from, int to, int bonus) {
    int fromPos = posToValue(from);
    historyHeuristic[fromPos][to] += bonus;
    //Half the entire history if a value reaches max.
    if (historyHeuristic[fromPos][to] >= maxHistoryValue) {
        maxHistoryValue <<= 1; // Double the new max
        for (int i = 0; i < 12; i++) {
            for (int j = 0; j < 64; j++) {
                historyHeuristic[i][j] >>= 1;
            }
        }
    }
}
/**
* Functions once used to convert book moves into my engines format
Move parseMove(const std::string& moveStr, Board board) {
    // King side castle
    if (moveStr == "'O-O'") {
        if (board.whiteToMove) {
            Move move(3, 1);
            return move;
        }
        else {
            Move move(59, 57);
            return move;
        }
    }
    else if (moveStr == "'O-O-O'") {
        if (board.whiteToMove) {
            Move move(3, 5);
            return move;
        }
        else {
            Move move(59, 61);
            return move;
        }
    }
    bool isCapture = moveStr.find('x') != std::string::npos;
    char promotion = 0;
    int startIndex = 1;
    int expectedLength = 4;
    if (isCapture) {
        expectedLength += 1;
        startIndex++;
    }
    if (moveStr.find('=') != std::string::npos) {
        promotion = moveStr.back();
        expectedLength += 2;
    }

    // Simplified example, does not handle disambiguation or pawn moves accurately
    std::string toPos;
    char pieceMoving = 'P';

    if (isupper(moveStr[1])) {
        // For non-pawn moves
        startIndex++;
        expectedLength++;
        pieceMoving = moveStr[1];
    }

    int file = 0;
    int rank = 0;
    // checking disambiguation
    if (size(moveStr) > expectedLength) {
        char disambig = isupper(moveStr[1]) ? moveStr[2] : moveStr[1];
        if (isalpha(disambig)) {
            file = 'h' - disambig;
        }
        else {
            rank = disambig - '1';
        }
        startIndex++;
    }

    toPos = moveStr.substr(startIndex, 2);
    int toIndex = boardPositionToIndex(toPos);

    std::vector<Move> moves;
    Bitboard ownPieces = board.whiteToMove ? board.whitePieces : board.blackPieces;
    Bitboard opponentPieces = board.whiteToMove ? board.blackPieces : board.whitePieces;
    if (pieceMoving == 'P') {
        moves = board.generatePawnMoves(board.whiteToMove ? board.whitePawns : board.blackPawns, ownPieces, opponentPieces);
    }
    else if (pieceMoving == 'N') {
        moves = board.generateKnightMoves(board.whiteToMove ? board.whiteKnights : board.blackKnights, ownPieces, opponentPieces);
    }
    else if (pieceMoving == 'B') {
        moves = board.generateBishopMoves(board.whiteToMove ? board.whiteBishops : board.blackBishops, ownPieces, opponentPieces);
    }
    else if (pieceMoving == 'R') {
        moves = board.generateRookMoves(board.whiteToMove ? board.whiteRooks : board.blackRooks, ownPieces, opponentPieces);
    }
    else if (pieceMoving == 'Q') {
        moves = board.generateQueenMoves(board.whiteToMove ? board.whiteQueens : board.blackQueens, ownPieces, opponentPieces);
    }
    else if (pieceMoving == 'K') {
        moves = board.generateKingMoves(board.whiteToMove ? board.whiteKing : board.blackKing, ownPieces, opponentPieces);
    }

    for (Move move : moves) {

        if (move.to == toIndex) {
            if (!file && !rank) {
                return move;
            }
            else if (file && (move.from % 8 == file)) {
                return move;
            }
            else if (rank && (move.from / 8 == rank)) {
                return move;
            }
        }
    }
    return Move();
}

void convertMoves(const std::string& input, Board& board) {
    std::vector<Move> moveList;
    std::istringstream inputStream(input);
    std::string line;
    while (std::getline(inputStream, line)) {
        if (line.find('{') != std::string::npos && line.find('}') != std::string::npos) {
            std::cout << board.countTranspositionTableEntries() << std::endl;
            board.createBoard();
            std::vector<std::string> movesVec;
            std::string movesStr = line.substr(line.find('{') + 1, line.find('}') - line.find('{') - 1);
            std::istringstream movesStream(movesStr);
            std::string move;
            while (std::getline(movesStream, move, ',')) {
                move.erase(remove_if(move.begin(), move.end(), isspace), move.end()); // Remove spaces
                movesVec.push_back(move);
            }

            for (const auto& moveStr : movesVec) {
                Move move = parseMove(moveStr, board);
                if (move.from == -1) {
                    board.printBoard();
                    std::cout << move.from << move.to << std::endl;
                    break;
                }
                board.makeMove(move);
                uint64_t hash = board.generateZobristHash();
                TT_Entry* ttEntry = board.probeTranspositionTable(hash);
                board.record_tt_entry(hash, 0, HASH_BOOK, move, 0);
            }
        }
    }
}

*/
