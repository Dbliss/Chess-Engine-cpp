#include "chess.h"
#include <iostream>
#include <sstream>
#include <string>
#include <intrin.h>
#include <chrono>
#include "zobrist.h"

#include <algorithm>
#include <fstream>
#include <cmath>
#include <bit>

const Move NO_MOVE;

constexpr Bitboard NOT_A_FILE = 0x7F7F7F7F7F7F7F7FULL;
constexpr Bitboard NOT_H_FILE = 0xFEFEFEFEFEFEFEFEULL;

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

unsigned int ctzll(unsigned long long x) {
    unsigned long index; // Variable to store the result
    // _BitScanForward64 returns 0 if x is zero, so handle this case:
    if (_BitScanForward64(&index, x))
        return index;
    else
        return 64; // Define behavior for x == 0
}

// Constructor to initialize the board
Board::Board() {
    createBoard();
    initializeZobristTable();
    resize_tt(64);  // Calls the resize function to allocate memory
    clear_tt();     // Clear the table to reset all entries
    loadOpeningBook();
    maxHistoryValue = 0x0000000000000100ULL;
    //std::cout << "Number of entries in the transposition table: " << countTranspositionTableEntries() << std::endl;
}

// Function to initialize the board with the starting positions
void Board::createBoard() {
    // Initialize the board with starting positions
    whitePawns = 0x000000000000FF00ULL;
    blackPawns = 0x00FF000000000000ULL;
    whiteRooks = 0x0000000000000081ULL;
    blackRooks = 0x8100000000000000ULL;
    whiteKnights = 0x0000000000000042ULL;
    blackKnights = 0x4200000000000000ULL;
    whiteBishops = 0x0000000000000024ULL;
    blackBishops = 0x2400000000000000ULL;
    whiteQueens = 0x0000000000000010ULL;
    blackQueens = 0x1000000000000000ULL;
    whiteKing = 0x0000000000000008ULL;
    blackKing = 0x0800000000000000ULL;
    enPassantTarget = 0x0ULL;
    whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
    blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;
    whiteToMove = true;

    whiteKingMoved = false;
    whiteLRookMoved = false;
    whiteRRookMoved = false;
    blackKingMoved = false;
    blackLRookMoved = false;
    blackRRookMoved = false;

    positionHistory.clear();
    updatePositionHistory(true);   // count the initial position as occurrence #1
}

void Board::createBoardFromFEN(const std::string& fen) {
    parseFEN(fen, *this);
    positionHistory.clear();
    updatePositionHistory(true);
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
    int epIndex = ctzll(enPassantTarget); // using GCC built-in function to find the index of the lowest set bit
    std::string enPassant = "-";
    if (enPassantTarget != 0 && epIndex >= 16 && epIndex <= 55) { // Valid en passant ranks
        enPassant = numToBoardPosition(epIndex);
    }

    // Determine who's move it is
    char playerToMove = whiteToMove ? 'w' : 'b';

    // Complete FEN string with game state
    fenStream << ' ' << playerToMove << ' ' << castlingRights << ' ' << enPassant << " 0 1";

    // Output FEN string
    std::cout << "FEN: " << fenStream.str() << std::endl;
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

std::vector<Move> Board::generatePawnMoves(Bitboard pawns, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    moves.reserve(64);
    Bitboard emptySquares = ~(ownPieces | opponentPieces);
    Bitboard promotionRank = whiteToMove ? 0xFF00000000000000ULL : 0x00000000000000FFULL;

    // Single pawn moves
    Bitboard singlePush = whiteToMove ? (pawns << 8) & emptySquares : (pawns >> 8) & emptySquares;
    Bitboard singlePushMask = singlePush;
    while (singlePushMask) {
        int to = ctzll(singlePushMask);
        int from = whiteToMove ? to - 8 : to + 8;
        singlePushMask &= singlePushMask - 1;
        if ((1ULL << to) & promotionRank) {
            moves.emplace_back(from, to, 'q');
            moves.emplace_back(from, to, 'r');
            moves.emplace_back(from, to, 'b');
            moves.emplace_back(from, to, 'n');
        }
        else {
            moves.emplace_back(from, to);
        }
    }

    // Double pawn moves (only from the starting position)
    Bitboard startRankMask = whiteToMove ? 0x000000000000FF00ULL : 0x00FF000000000000ULL;
    Bitboard doublePush = whiteToMove ? ((pawns & startRankMask) << 16) & (emptySquares << 8) & emptySquares
        : ((pawns & startRankMask) >> 16) & (emptySquares >> 8) & emptySquares;
    Bitboard doublePushMask = doublePush;
    while (doublePushMask) {
        int to = ctzll(doublePushMask);
        int from = whiteToMove ? to - 16 : to + 16;
        doublePushMask &= doublePushMask - 1;
        moves.emplace_back(from, to);
    }

    // Pawn captures
    Bitboard leftCaptures = whiteToMove ? (pawns << 9) & opponentPieces & NOT_H_FILE
        : (pawns >> 9) & opponentPieces & NOT_A_FILE;
    Bitboard rightCaptures = whiteToMove ? (pawns << 7) & opponentPieces & NOT_A_FILE
        : (pawns >> 7) & opponentPieces & NOT_H_FILE;
    int from;
    while (leftCaptures) {
        int to = ctzll(leftCaptures);
        from = whiteToMove ? to - 9 : to + 9;
        leftCaptures &= leftCaptures - 1;
        Move move(from, to);
        move.isCapture = true;
        if ((1ULL << to) & promotionRank) {
            move.promotion = 'q';
            moves.push_back(move);
            move.promotion = 'r';
            moves.push_back(move);
            move.promotion = 'b';
            moves.push_back(move);
            move.promotion = 'n';
            moves.push_back(move);
        }
        else {
            moves.push_back(move);
        }
    }

    while (rightCaptures) {
        int to = ctzll(rightCaptures);
        from = whiteToMove ? to - 7 : to + 7;
        rightCaptures &= rightCaptures - 1;
        Move move(from, to);
        move.isCapture = true;
        if ((1ULL << to) & promotionRank) {
            move.promotion = 'q';
            moves.push_back(move);
            move.promotion = 'r';
            moves.push_back(move);
            move.promotion = 'b';
            moves.push_back(move);
            move.promotion = 'n';
            moves.push_back(move);
        }
        else {
            moves.push_back(move);
        }
    }

    // en passant
    Bitboard leftPassant = whiteToMove ? (pawns << 9) & enPassantTarget & NOT_H_FILE
        : (pawns >> 9) & enPassantTarget & NOT_A_FILE;
    Bitboard rightPassant = whiteToMove ? (pawns << 7) & enPassantTarget & NOT_A_FILE
        : (pawns >> 7) & enPassantTarget & NOT_H_FILE;
   
    while (leftPassant) {
        int to = ctzll(leftPassant);
        from = whiteToMove ? to - 9 : to + 9;
        leftPassant &= leftPassant - 1;
        Move move(from, to);
        move.isCapture = true;
        moves.push_back(move);
    }

    while (rightPassant) {
        int to = ctzll(rightPassant);
        from = whiteToMove ? to - 7 : to + 7;
        rightPassant &= rightPassant - 1;
        Move move(from, to);
        move.isCapture = true;
        moves.push_back(move);
    } 
    return moves;
}

std::vector<Move> Board::generateBishopMoves(Bitboard bishops, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    moves.reserve(32);
    Bitboard occupiedSquares = ownPieces | opponentPieces;
    const int directions[4] = { 9, 7, -9, -7 };

    while (bishops) {
        int from = ctzll(bishops);
        bishops &= bishops - 1;

        for (int dir : directions) {
            int to = from;
            while (true) {
                to += dir;
                if (to < 0 || to >= 64 || (to % 8 == 0 && dir == 9) || (to % 8 == 7 && dir == 7) ||
                    (to % 8 == 0 && dir == -7) || (to % 8 == 7 && dir == -9)) break;

                if (occupiedSquares & (1ULL << to)) {
                    if (opponentPieces & (1ULL << to)) {
                        Move move(from, to);
                        move.isCapture = true;
                        moves.push_back(move);
                    }
                    break;
                }
                else {
                    moves.emplace_back(from, to);
                }
            }
        }
    }
    return moves;
}

std::vector<Move> Board::generateRookMoves(Bitboard rooks, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    moves.reserve(64);
    Bitboard occupiedSquares = ownPieces | opponentPieces;
    const int directions[4] = { 8, -8, 1, -1 };
    while (rooks) {
        int from = ctzll(rooks);
        rooks &= rooks - 1;
        for (int dir : directions) {
            int to = from;
            while (true) {
                to += dir;

                // Prevent the rook from wrapping around the board edges
                if (to < 0 || to >= 64 ||
                    (dir == 1 && (to % 8 == 0)) ||
                    (dir == -1 && (to % 8 == 7))) break;

                if (occupiedSquares & (1ULL << to)) {
                    if (opponentPieces & (1ULL << to)) {
                        Move move(from, to);
                        move.isCapture = true;
                        moves.push_back(move);
                    }
                    break;
                }
                else {
                    moves.emplace_back(from, to);
                }
            }
        }
    }
    return moves;
}

std::vector<Move> Board::generateKnightMoves(Bitboard knights, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    moves.reserve(32);
    const int knightMoves[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };

    while (knights) {
        int from = ctzll(knights);
        knights &= knights - 1;

        for (int move : knightMoves) {
            int to = from + move;
            if (to < 0 || to >= 64 || (abs(from % 8 - to % 8) > 2)) continue;

            if (!(ownPieces & (1ULL << to))) {
                if (opponentPieces & (1ULL << to)) {
                    Move move(from, to);
                    move.isCapture = true;
                    moves.push_back(move);
                }
                else {
                    moves.emplace_back(from, to);
                }
            }
        }
    }
    return moves;
}

std::vector<Move> Board::generateKingMoves(Bitboard kingBitboard, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    moves.reserve(16);

    if (!kingBitboard) return moves;

    const int kingSteps[8] = { 8, -8, 1, -1, 9, 7, -9, -7 };

    const int kingFromSquare = ctzll(kingBitboard);
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

    // Generate legal king steps WITHOUT make/undo
    for (int step : kingSteps) {
        int kingToSquare = kingFromSquare + step;

        // edge checks (keep exactly as you had them)
        if (kingToSquare < 0 || kingToSquare >= 64 ||
            (kingFromSquare % 8 == 0 && (step == -1 || step == 7 || step == -9)) ||
            (kingFromSquare % 8 == 7 && (step == 1  || step == -7 || step == 9)))
            continue;

        const Bitboard kingToMask = 1ULL << kingToSquare;

        if (ownPieces & kingToMask) continue;

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
        if (isSquareAttacked(
                kingToSquare,
                attackersAreWhite,
                occupiedAfterKingMove,
                pawnsAfterCapture,
                knightsAfterCapture,
                bishopsAfterCapture,
                rooksAfterCapture,
                queensAfterCapture,
                kingAfterCapture
            )) continue;

        Move m(kingFromSquare, kingToSquare);
        if (opponentPieces & kingToMask) m.isCapture = true;
        moves.push_back(m);
    }

    // Castling: not currently in check, squares between empty, and BOTH crossed squares not attacked.
    const bool kingCurrentlyInCheck =
        isSquareAttacked(kingFromSquare, attackersAreWhite, allOccupied,
                         attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing);

    if (!kingCurrentlyInCheck) {
        if (whiteToMove) {
            // White kingside
            if (!whiteKingMoved && !whiteRRookMoved && (whiteRooks & 0x0000000000000001ULL)) {
                const Bitboard emptyBetweenMask = 0x0000000000000006ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked(kingFromSquare - 1, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing) &&
                        !isSquareAttacked(kingFromSquare - 2, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing)) {
                        moves.emplace_back(kingFromSquare, kingFromSquare - 2);
                    }
                }
            }

            // White queenside
            if (!whiteKingMoved && !whiteLRookMoved && (whiteRooks & 0x0000000000000080ULL)) {
                const Bitboard emptyBetweenMask = 0x0000000000000070ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked(kingFromSquare + 1, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing) &&
                        !isSquareAttacked(kingFromSquare + 2, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing)) {
                        moves.emplace_back(kingFromSquare, kingFromSquare + 2);
                    }
                }
            }
        } else {
            // Black kingside
            if (!blackKingMoved && !blackRRookMoved && (blackRooks & 0x0100000000000000ULL)) {
                const Bitboard emptyBetweenMask = 0x0600000000000000ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked(kingFromSquare - 1, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing) &&
                        !isSquareAttacked(kingFromSquare - 2, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing)) {
                        moves.emplace_back(kingFromSquare, kingFromSquare - 2);
                    }
                }
            }

            // Black queenside
            if (!blackKingMoved && !blackLRookMoved && (blackRooks & 0x8000000000000000ULL)) {
                const Bitboard emptyBetweenMask = 0x7000000000000000ULL;
                if ((allOccupied & emptyBetweenMask) == 0) {
                    if (!isSquareAttacked(kingFromSquare + 1, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing) &&
                        !isSquareAttacked(kingFromSquare + 2, attackersAreWhite, occupiedWithoutOurKing,
                                          attackerPawns, attackerKnights, attackerBishops, attackerRooks, attackerQueens, attackerKing)) {
                        moves.emplace_back(kingFromSquare, kingFromSquare + 2);
                    }
                }
            }
        }
    }
    return moves;
}

std::vector<Move> Board::generateQueenMoves(Bitboard queens, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    moves.reserve(128);
    std::vector<Move> bishopMoves = generateBishopMoves(queens, ownPieces, opponentPieces);
    std::vector<Move> rookMoves = generateRookMoves(queens, ownPieces, opponentPieces);

    moves.insert(moves.end(), bishopMoves.begin(), bishopMoves.end());
    moves.insert(moves.end(), rookMoves.begin(), rookMoves.end());
    return moves;
}

bool contains(const std::vector<int>& vec, int value) {
    return std::find(vec.begin(), vec.end(), value) != vec.end();
}

std::vector<Move> Board::generateAllMoves() {
    std::vector<Move> allMoves;
    allMoves.reserve(256);

    Bitboard ownPieces = whiteToMove ? whitePieces : blackPieces;
    Bitboard opponentPieces = whiteToMove ? blackPieces : whitePieces;

    std::vector<Move> pawnMoves   = generatePawnMoves(whiteToMove ? whitePawns   : blackPawns,   ownPieces, opponentPieces);
    std::vector<Move> bishopMoves = generateBishopMoves(whiteToMove ? whiteBishops : blackBishops, ownPieces, opponentPieces);
    std::vector<Move> rookMoves   = generateRookMoves(whiteToMove ? whiteRooks   : blackRooks,   ownPieces, opponentPieces);
    std::vector<Move> knightMoves = generateKnightMoves(whiteToMove ? whiteKnights : blackKnights, ownPieces, opponentPieces);
    std::vector<Move> kingMoves   = generateKingMoves(whiteToMove ? whiteKing    : blackKing,    ownPieces, opponentPieces);
    std::vector<Move> queenMoves  = generateQueenMoves(whiteToMove ? whiteQueens : blackQueens,  ownPieces, opponentPieces);

    allMoves.insert(allMoves.end(), pawnMoves.begin(), pawnMoves.end());
    allMoves.insert(allMoves.end(), bishopMoves.begin(), bishopMoves.end());
    allMoves.insert(allMoves.end(), rookMoves.begin(), rookMoves.end());
    allMoves.insert(allMoves.end(), knightMoves.begin(), knightMoves.end());
    allMoves.insert(allMoves.end(), queenMoves.begin(), queenMoves.end());

    Bitboard ownKing = whiteToMove ? whiteKing : blackKing;

    // Pieces that might be pinned are "key defenders"
    std::vector<Move> kingMobileMoves = generateQueenMoves(ownKing, opponentPieces, ownPieces);
    std::vector<int> keyDefenderCoords;
    keyDefenderCoords.reserve(16);
    for (Move& move : kingMobileMoves) {
        if (move.isCapture) keyDefenderCoords.push_back(move.to);
    }

    std::vector<Move> legalMoves;
    legalMoves.reserve(128);

    Bitboard store = enPassantTarget;

    bool currentlyInCheck = amIInCheck(whiteToMove);
    bool sideToMoveIsWhite = whiteToMove; // capture current side (whiteToMove toggles in makeMove)

    for (Move& move : allMoves) {
        // Detect en passant moves so they ALWAYS get checked for legality
        bool isEnPassantMove = false;
        if (store & (1ULL << move.to)) {
            // EP target square matches destination, and the moving piece is a pawn moving diagonally
            if (sideToMoveIsWhite) {
                if ((whitePawns & (1ULL << move.from)) && (move.to == move.from + 7 || move.to == move.from + 9)) {
                    isEnPassantMove = true;
                }
            } else {
                if ((blackPawns & (1ULL << move.from)) && (move.to == move.from - 7 || move.to == move.from - 9)) {
                    isEnPassantMove = true;
                }
            }
        }

        // Fast-path: only allowed if NOT in check, NOT a key defender, AND NOT en passant
        if (!currentlyInCheck && !isEnPassantMove && !contains(keyDefenderCoords, move.from)) {
            legalMoves.push_back(move);
            continue;
        }
        Undo u;
        makeMoveFast(move, u);
        if (!amIInCheck(!whiteToMove)) {
            legalMoves.push_back(move);
        }
        undoMoveFast(move, u);
    }

    // kingMoves already returned legal moves from generateKingMoves()
    legalMoves.insert(legalMoves.end(), kingMoves.begin(), kingMoves.end());
    return legalMoves;
}

bool Board::amIInCheck(bool player) {
    Bitboard ownKing = player ? whiteKing : blackKing;
    Bitboard opponentBishops = player ? blackBishops : whiteBishops;
    Bitboard opponentQueens = player ? blackQueens : whiteQueens;
    Bitboard opponentKing = player ? blackKing : whiteKing;
    Bitboard ownPieces = player ? whitePieces : blackPieces;
    Bitboard enemyPieces = player ? blackPieces : whitePieces;

    int kingPos = ctzll(ownKing);

    // Check for bishop/queen diagonal attacks
    const int bishopDirections[4] = { 9, 7, -9, -7 };
    for (int dir : bishopDirections) {
        int to = kingPos;
        while (true) {
            to += dir;
            if (to < 0 || to >= 64 || (to % 8 == 0 && (dir == 9 || dir == -7)) || (to % 8 == 7 && (dir == 7 || dir == -9))) break;
            Bitboard posMask = 1ULL << to;
            if (opponentBishops & posMask || opponentQueens & posMask) return true;
            if (ownPieces & posMask || enemyPieces & posMask) break;
        }
    }

    // Check for rook/queen straight attacks
    Bitboard opponentRooks = player ? blackRooks : whiteRooks;
    const int rookDirections[4] = { 8, -8, 1, -1 };
    for (int dir : rookDirections) {
        int to = kingPos;
        while (true) {
            to += dir;
            if (to < 0 || to >= 64 || (to % 8 == 0 && dir == 1) || (to % 8 == 7 && dir == -1)) break;
            Bitboard posMask = 1ULL << to;
            if (opponentRooks & posMask || opponentQueens & posMask) return true;
            if (ownPieces & posMask || enemyPieces & posMask)  break;
        }
    }

    // Check for pawn attacks
    Bitboard opponentPawns = player ? blackPawns : whitePawns;
    Bitboard pawnAttacks = player ? ((opponentPawns >> 7) & NOT_H_FILE) | ((opponentPawns >> 9) & NOT_A_FILE)
        : ((opponentPawns << 7) & NOT_A_FILE) | ((opponentPawns << 9) & NOT_H_FILE);


    if (pawnAttacks & ownKing) {
        return true;
    }

    // Check for knight attacks
    const int knightMoves[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };
    Bitboard opponentKnights = player ? blackKnights : whiteKnights;
    for (int move : knightMoves) {
        int to = kingPos + move;
        if (to >= 0 && to < 64 && abs((kingPos % 8) - (to % 8)) <= 2) {
            if (opponentKnights & (1ULL << to)) return true;
        }
    }

    // Check for king attacks
    const int kingMoves[8] = { 8, -8, 1, -1, 9, 7, -9, -7 };
    for (int move : kingMoves) {
        int to = kingPos + move;
        if (to >= 0 && to < 64 && abs((kingPos % 8) - (to % 8)) <= 1) {
            if (opponentKing & (1ULL << to)) return true;
        }
    }

    return false;
}

void Board::makeMove(Move& move, Undo& u) {
    makeMoveFast(move, u);
    updatePositionHistory(true);
}

void Board::undoMove(const Move& move, const Undo& u) {
    updatePositionHistory(false);
    undoMoveFast(move, u);
}

void Board::makeNullMove(Undo& u) {
    // save state
    u.prevEnPassantTarget = enPassantTarget;

    u.prevWhiteKingMoved  = whiteKingMoved;
    u.prevWhiteLRookMoved = whiteLRookMoved;
    u.prevWhiteRRookMoved = whiteRRookMoved;

    u.prevBlackKingMoved  = blackKingMoved;
    u.prevBlackLRookMoved = blackLRookMoved;
    u.prevBlackRRookMoved = blackRRookMoved;

    // null move clears EP
    enPassantTarget = 0;

    // toggle side
    whiteToMove = !whiteToMove;
}

void Board::undoNullMove(const Undo& u) {
    // toggle side back
    whiteToMove = !whiteToMove;

    // restore state
    enPassantTarget = u.prevEnPassantTarget;

    whiteKingMoved  = u.prevWhiteKingMoved;
    whiteLRookMoved = u.prevWhiteLRookMoved;
    whiteRRookMoved = u.prevWhiteRRookMoved;

    blackKingMoved  = u.prevBlackKingMoved;
    blackLRookMoved = u.prevBlackLRookMoved;
    blackRRookMoved = u.prevBlackRRookMoved;
}

void Board::makeMoveFast(Move& move, Undo& u) {
    // ---- store undo state ----
    u.prevEnPassantTarget = enPassantTarget;

    u.prevWhiteKingMoved  = whiteKingMoved;
    u.prevWhiteLRookMoved = whiteLRookMoved;
    u.prevWhiteRRookMoved = whiteRRookMoved;

    u.prevBlackKingMoved  = blackKingMoved;
    u.prevBlackLRookMoved = blackLRookMoved;
    u.prevBlackRRookMoved = blackRRookMoved;

    u.capturedPiece = 0;
    u.wasEnPassant  = false;

    // EP exists for ONE ply only
    enPassantTarget = 0;

    Bitboard fromMask = 1ULL << move.from;
    Bitboard toMask   = 1ULL << move.to;

    if (whiteToMove) {
        // ---------------- WHITE MOVE ----------------
        if (whitePawns & fromMask) {
            whitePawns ^= fromMask | toMask;

            // double pawn push → create EP square
            if (move.to == move.from + 16)
                enPassantTarget = 1ULL << (move.from + 8);

            // promotion
            if (move.promotion) {
                whitePawns &= ~toMask;
                switch (move.promotion) {
                    case 'q': whiteQueens  |= toMask; break;
                    case 'r': whiteRooks   |= toMask; break;
                    case 'b': whiteBishops |= toMask; break;
                    case 'n': whiteKnights |= toMask; break;
                }
            }
        }
        else if (whiteRooks & fromMask) {
            whiteRooks ^= fromMask | toMask;
            if (move.from == 0) whiteRRookMoved = true;
            if (move.from == 7) whiteLRookMoved = true;
        }
        else if (whiteKnights & fromMask) whiteKnights ^= fromMask | toMask;
        else if (whiteBishops & fromMask) whiteBishops ^= fromMask | toMask;
        else if (whiteQueens  & fromMask) whiteQueens  ^= fromMask | toMask;
        else if (whiteKing    & fromMask) {
            whiteKing ^= fromMask | toMask;
            whiteKingMoved = true;

            // castling rook movement
            if (move.to == move.from - 2)      whiteRooks ^= 0x0000000000000005ULL; // king-side
            else if (move.to == move.from + 2) whiteRooks ^= 0x0000000000000090ULL; // queen-side
        }

        // captures
        if (move.isCapture) {
            if      (blackPawns   & toMask) { u.capturedPiece='p'; blackPawns   &=~toMask; }
            else if (blackRooks   & toMask) { u.capturedPiece='r'; blackRooks   &=~toMask; }
            else if (blackKnights & toMask) { u.capturedPiece='n'; blackKnights &=~toMask; }
            else if (blackBishops & toMask) { u.capturedPiece='b'; blackBishops &=~toMask; }
            else if (blackQueens  & toMask) { u.capturedPiece='q'; blackQueens  &=~toMask; }
            else if (blackKing    & toMask) { u.capturedPiece='k'; blackKing    &=~toMask; }
            else if (u.prevEnPassantTarget & toMask) {
                // en-passant capture: capture pawn behind EP target square
                u.wasEnPassant  = true;
                u.capturedPiece = 'p';
                blackPawns &= ~(toMask >> 8);
            }
        }
    }
    else {
        // ---------------- BLACK MOVE ----------------
        if (blackPawns & fromMask) {
            blackPawns ^= fromMask | toMask;

            if (move.to == move.from - 16)
                enPassantTarget = 1ULL << (move.from - 8);

            if (move.promotion) {
                blackPawns &= ~toMask;
                switch (move.promotion) {
                    case 'q': blackQueens  |= toMask; break;
                    case 'r': blackRooks   |= toMask; break;
                    case 'b': blackBishops |= toMask; break;
                    case 'n': blackKnights |= toMask; break;
                }
            }
        }
        else if (blackRooks & fromMask) {
            blackRooks ^= fromMask | toMask;
            if (move.from == 56) blackRRookMoved = true;
            if (move.from == 63) blackLRookMoved = true;
        }
        else if (blackKnights & fromMask) blackKnights ^= fromMask | toMask;
        else if (blackBishops & fromMask) blackBishops ^= fromMask | toMask;
        else if (blackQueens  & fromMask) blackQueens  ^= fromMask | toMask;
        else if (blackKing    & fromMask) {
            blackKing ^= fromMask | toMask;
            blackKingMoved = true;

            if (move.to == move.from - 2)      blackRooks ^= 0x0500000000000000ULL; // king-side
            else if (move.to == move.from + 2) blackRooks ^= 0x9000000000000000ULL; // queen-side
        }

        if (move.isCapture) {
            if      (whitePawns   & toMask) { u.capturedPiece='p'; whitePawns   &=~toMask; }
            else if (whiteRooks   & toMask) { u.capturedPiece='r'; whiteRooks   &=~toMask; }
            else if (whiteKnights & toMask) { u.capturedPiece='n'; whiteKnights &=~toMask; }
            else if (whiteBishops & toMask) { u.capturedPiece='b'; whiteBishops &=~toMask; }
            else if (whiteQueens  & toMask) { u.capturedPiece='q'; whiteQueens  &=~toMask; }
            else if (whiteKing    & toMask) { u.capturedPiece='k'; whiteKing    &=~toMask; }
            else if (u.prevEnPassantTarget & toMask) {
                u.wasEnPassant  = true;
                u.capturedPiece = 'p';
                whitePawns &= ~(toMask << 8);
            }
        }
    }

    whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
    blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;

    whiteToMove = !whiteToMove;
}

void Board::undoMoveFast(const Move& move, const Undo& u) {
    enPassantTarget = u.prevEnPassantTarget;

    whiteKingMoved  = u.prevWhiteKingMoved;
    whiteLRookMoved = u.prevWhiteLRookMoved;
    whiteRRookMoved = u.prevWhiteRRookMoved;

    blackKingMoved  = u.prevBlackKingMoved;
    blackLRookMoved = u.prevBlackLRookMoved;
    blackRRookMoved = u.prevBlackRRookMoved;

    Bitboard fromMask = 1ULL << move.from;
    Bitboard toMask   = 1ULL << move.to;

    if (!whiteToMove) {
        // undo WHITE move (because side-to-move was toggled in makeMoveFast)
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

    whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
    blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;

    whiteToMove = !whiteToMove;
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

    // Handle the en passant target square if there is one
    if (enPassant != "-") {
        int file = enPassant[0] - 'a';  // 'a' to 'h' -> 0 to 7
        int rank = enPassant[1] - '1';  // '1' to '8' -> 0 to 7
        int enPassantSquare = (rank) * 8 + (7 - file); // Convert to 0-63 indexing adjusted for your board setup
        board.enPassantTarget = 1ULL << enPassantSquare;
    }

    // Update the overall piece bitboards
    board.whitePieces = board.whitePawns | board.whiteRooks | board.whiteKnights |
        board.whiteBishops | board.whiteQueens | board.whiteKing;
    board.blackPieces = board.blackPawns | board.blackRooks | board.blackKnights |
        board.blackBishops | board.blackQueens | board.blackKing;
}

char Board::getPieceAt(int index) const {
    if (index < 0 || index >= 64) return ' ';

    if (whitePawns & (1ULL << index)) return 'p';
    if (whiteRooks & (1ULL << index)) return 'r';
    if (whiteKnights & (1ULL << index)) return 'n';
    if (whiteBishops & (1ULL << index)) return 'b';
    if (whiteQueens & (1ULL << index)) return 'q';
    if (whiteKing & (1ULL << index)) return 'k';

    if (blackPawns & (1ULL << index)) return 'P';
    if (blackRooks & (1ULL << index)) return 'R';
    if (blackKnights & (1ULL << index)) return 'N';
    if (blackBishops & (1ULL << index)) return 'B';
    if (blackQueens & (1ULL << index)) return 'Q';
    if (blackKing & (1ULL << index)) return 'K';

    return ' ';
}

int Board::getEnPassantFile() const {
    int index = ctzll(enPassantTarget);
    int file = index % 8;
    return file;
}

int Board::getPieceIndex(char piece) const {
    switch (piece) {
    case 'p': return 0;
    case 'n': return 1;
    case 'b': return 2;
    case 'r': return 3;
    case 'q': return 4;
    case 'k': return 5;
    case 'P': return 6;
    case 'N': return 7;
    case 'B': return 8;
    case 'R': return 9;
    case 'Q': return 10;
    case 'K': return 11;
    default: return -1;
    }
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
    if (enPassantTarget) {
        int file = getEnPassantFile(); // A function to get the file of the en passant target
        hash ^= zobristEnPassant[file];
    }

    // Add side to move to the hash
    if (whiteToMove) {
        hash ^= zobristSideToMove;
    }

    return hash;
}

void Board::updatePositionHistory(bool plus) {
    uint64_t hash = generateZobristHash();
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
    uint64_t hash = generateZobristHash();
    auto it = positionHistory.find(hash);
    int count = (it == positionHistory.end()) ? 0 : it->second;
    return count >= 3;;
}

bool Board::isThreefoldRepetition(uint64_t hash) {
    return positionHistory[hash] >= 2;
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

std::vector<std::pair<Move, uint64_t>> orderMoves(Board& board, const std::vector<Move>& moves, TT_Entry* ttEntry, int depth) {
    Move hashMove = (ttEntry && ttEntry->depth >= (depth >> 1)) ? ttEntry->move : NO_MOVE;

    std::vector<std::pair<Move, uint64_t>> allMoves;

    for (const Move& move : moves) {
        uint64_t score = 0;
        int captureStrength;
        if (move == hashMove) {
            allMoves.push_back({ move, board.maxHistoryValue + 100 });
        }
        else if (move.isCapture || move.promotion) {
            captureStrength = isGoodCapture(move, board);
            score += captureStrength;
            if (captureStrength < 0) {
                allMoves.push_back({ move,  score });
                continue;
            }
            else if (move.promotion) {
                score += getPieceValue(move.promotion);
            }
            allMoves.push_back({ move,  score + board.maxHistoryValue + 1 });
        }
        else if (isKillerMove(move, board, depth)) {
            allMoves.push_back({ move, board.maxHistoryValue });
        }
        else {
            score = board.historyHeuristic[board.posToValue(move.from)][move.to]; // This should be between 0-maxHistoryValue
            allMoves.push_back({ move, score });
        }
    }

    // Sort non-captures based on history heuristic  
    std::sort(allMoves.begin(), allMoves.end(), [](const std::pair<Move, uint64_t>& a, const std::pair<Move, uint64_t>& b) {
        return a.second > b.second;
        });
    return allMoves;
}

std::vector<Move> orderMoves2(Board& board, const std::vector<Move>& moves, TT_Entry* ttEntry, int depth) {
    Move hashMove = (ttEntry && ttEntry->depth >= (depth >> 1)) ? ttEntry->move : NO_MOVE;

    std::vector<std::pair<Move, uint64_t>> allMoves;

    for (const Move& move : moves) {
        uint64_t score = 0;
        int captureStrength;
        if (move == hashMove) {
            allMoves.push_back({ move, board.maxHistoryValue + 100 });
        }
        else if (move.isCapture || move.promotion) {
            captureStrength = isGoodCapture(move, board);
            score += captureStrength;
            if (captureStrength < 0) {
                allMoves.push_back({ move,  score});
                continue;
            }
            else if (move.promotion) {
                score += getPieceValue(move.promotion);
            }
            allMoves.push_back({ move,  score + board.maxHistoryValue + 1 });
        }
        else if (isKillerMove(move, board, depth)) {
            allMoves.push_back({ move, board.maxHistoryValue });
        }
        else {
            score = board.historyHeuristic[board.posToValue(move.from)][move.to]; // This should be between 0-maxHistoryValue
            allMoves.push_back({ move, score });
        }
    }

    // Sort non-captures based on history heuristic  
    std::sort(allMoves.begin(), allMoves.end(), [](const std::pair<Move, uint64_t>& a, const std::pair<Move, uint64_t>& b) {
        return a.second > b.second;
        });


    // Combine all move categories into the final ordered list
    std::vector<Move> orderedMoves;
    for (const auto& pair : allMoves) orderedMoves.push_back(pair.first);

    return orderedMoves;
}

bool isTacticalPosition(std::vector<Move> moves, Board board) {
    for (const Move& move : moves) {
        if (isGoodCapture(move, board) || isEqualCapture(move, board) || move.promotion) {
            return true;
        }
    }
    return false;
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
    return &transposition_table[hash % transposition_table.size()];
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
    if (moveStr.size() != 4) return Move(-1, -1); // Invalid move string
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
