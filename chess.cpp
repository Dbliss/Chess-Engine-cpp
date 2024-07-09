#include "chess.h"
#include <iostream>
#include <sstream>
#include <string>
#include <intrin.h>
#include <chrono>
#include "zobrist.h"

const Move NO_MOVE;

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
    resize_tt(64);  // Calls the resize function to allocate memory
    clear_tt();     // Clear the table to reset all entries
}

// Function to initialize the board with the starting positions
void Board::createBoard() {
    // Initialize the board with starting positions
    whitePawns = 0x000000000000FF00;
    blackPawns = 0x00FF000000000000;
    whiteRooks = 0x0000000000000081;
    blackRooks = 0x8100000000000000;
    whiteKnights = 0x0000000000000042;
    blackKnights = 0x4200000000000000;
    whiteBishops = 0x0000000000000024;
    blackBishops = 0x2400000000000000;
    whiteQueens = 0x0000000000000010;
    blackQueens = 0x1000000000000000;
    whiteKing = 0x0000000000000008;
    blackKing = 0x0800000000000000;
    enPassantTarget = 0x0;
    whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
    blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;
    whiteToMove = true;

    whiteKingMoved = false;
    whiteLRookMoved = false;
    whiteRRookMoved = false;
    blackKingMoved = false;
    blackLRookMoved = false;
    blackRRookMoved = false;
}

void Board::createBoardFromFEN(const std::string& fen) {
    parseFEN(fen, *this);
}

void Board::printBoard() {
    auto getPieceChar = [this](int index) -> char {
        if ((whitePawns >> index) & 1) return 'p';
        if ((blackPawns >> index) & 1) return 'P';
        if ((whiteRooks >> index) & 1) return 'r';
        if ((blackRooks >> index) & 1) return 'R';
        if ((whiteKnights >> index) & 1) return 'n';
        if ((blackKnights >> index) & 1) return 'N';
        if ((whiteBishops >> index) & 1) return 'b';
        if ((blackBishops >> index) & 1) return 'B';
        if ((whiteQueens >> index) & 1) return 'q';
        if ((blackQueens >> index) & 1) return 'Q';
        if ((whiteKing >> index) & 1) return 'k';
        if ((blackKing >> index) & 1) return 'K';
        return '.';
        };

    for (int rank = 7; rank >= 0; --rank) {
        for (int file = 7; file >= 0; --file) {
            int index = rank * 8 + file;
            std::cout << getPieceChar(index) << " ";
        }
        std::cout << "\n";
    }
    std::cout << std::endl;
}

std::vector<Move> Board::generatePawnMoves(Bitboard pawns, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    Bitboard emptySquares = ~(ownPieces | opponentPieces);
    Bitboard promotionRank = whiteToMove ? 0xFF00000000000000 : 0x00000000000000FF;

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
    Bitboard startRankMask = whiteToMove ? 0x000000000000FF00 : 0x00FF000000000000;
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
    Bitboard leftCaptures = whiteToMove ? (pawns << 9) & opponentPieces & 0xFEFEFEFEFEFEFEFE
        : (pawns >> 9) & opponentPieces & 0x7F7F7F7F7F7F7F7F;
    Bitboard rightCaptures = whiteToMove ? (pawns << 7) & opponentPieces & 0x7F7F7F7F7F7F7F7F
        : (pawns >> 7) & opponentPieces & 0xFEFEFEFEFEFEFEFE;
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
    Bitboard leftPassant = whiteToMove ? (pawns << 9) & enPassantTarget & 0xFEFEFEFEFEFEFEFE
        : (pawns >> 9) & enPassantTarget & 0x7F7F7F7F7F7F7F7F;
    Bitboard rightPassant = whiteToMove ? (pawns << 7) & enPassantTarget & 0x7F7F7F7F7F7F7F7F
        : (pawns >> 7) & enPassantTarget & 0xFEFEFEFEFEFEFEFE;
   
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

std::vector<Move> Board::generateKingMoves(Bitboard king, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    const int kingMoves[8] = { 8, -8, 1, -1, 9, 7, -9, -7 };

    int from = ctzll(king);
    for (int move : kingMoves) {
        int to = from + move;
        if (to < 0 || to >= 64 || (from % 8 == 0 && (move == -1 || move == 7 || move == -9)) ||
            (from % 8 == 7 && (move == 1 || move == -7 || move == 9))) continue;

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

    // Add castling moves
    if (whiteToMove) {
        // Kingside castling
        if (!whiteKingMoved && !whiteRRookMoved && !(ownPieces & 0x0000000000000006) && !(opponentPieces & 0x0000000000000006) && (whiteRooks & 0x0000000000000001)) {
            if (!amIInCheck(whiteToMove) && !isSquareAttacked(from - 1, whiteToMove)) {
                moves.emplace_back(from, from - 2); // Kingside castling
            }
        }
        // Queenside castling
        if (!whiteKingMoved && !whiteLRookMoved && !(ownPieces & 0x0000000000000070) && !(opponentPieces & 0x0000000000000070) && (whiteRooks & 0x0000000000000080)) {
            if (!amIInCheck(whiteToMove) && !isSquareAttacked(from + 1, whiteToMove)) {
                moves.emplace_back(from, from + 2); // Queenside castling
            }
        }
    }
    else {
        // Kingside castling
        if (!blackKingMoved && !blackRRookMoved && !(ownPieces & 0x600000000000000) && !(opponentPieces & 0x600000000000000) && (blackRooks & 0x0100000000000000)) {
            if (!amIInCheck(whiteToMove) && !isSquareAttacked(from - 1, whiteToMove)) {
                moves.emplace_back(from, from - 2); // Kingside castling
            }
        }
        // Queenside castling
        if (!blackKingMoved && !blackLRookMoved && !(ownPieces & 0x7000000000000000) && !(opponentPieces & 0x7000000000000000) && (blackRooks & 0x8000000000000000)) {
            if (!amIInCheck(whiteToMove) && !isSquareAttacked(from + 1, whiteToMove)) {
                moves.emplace_back(from, from + 2); // Queenside castling
            }
        }
    }

    return moves;
}

bool Board::isSquareAttacked(int square, bool byWhite) {
    Bitboard opponentPawns = byWhite ? blackPawns : whitePawns;
    Bitboard opponentRooks = byWhite ? blackRooks : whiteRooks;
    Bitboard opponentKnights = byWhite ? blackKnights : whiteKnights;
    Bitboard opponentBishops = byWhite ? blackBishops : whiteBishops;
    Bitboard opponentQueens = byWhite ? blackQueens : whiteQueens;
    Bitboard opponentKing = byWhite ? blackKing : whiteKing;

    // Check for pawn attacks
    Bitboard pawnAttacks = byWhite ? ((opponentPawns >> 7) & 0xFEFEFEFEFEFEFEFE) | ((opponentPawns >> 9) & 0x7F7F7F7F7F7F7F7F)
        : ((opponentPawns << 7) & 0x7F7F7F7F7F7F7F7F) | ((opponentPawns << 9) & 0xFEFEFEFEFEFEFEFE);
    if (pawnAttacks & (1ULL << square)) return true;

    // Check for knight attacks
    const int knightMoves[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };
    for (int move : knightMoves) {
        int to = square + move;
        if (to >= 0 && to < 64 && abs((square % 8) - (to % 8)) <= 2) {
            if (opponentKnights & (1ULL << to)) return true;
        }
    }

    // Check for bishop/queen diagonal attacks
    const int bishopDirections[4] = { 9, 7, -9, -7 };
    for (int dir : bishopDirections) {
        int to = square;
        while (true) {
            to += dir;
            if (to < 0 || to >= 64 || (to % 8 == 0 && (dir == 9 || dir == -7)) || (to % 8 == 7 && (dir == 7 || dir == -9))) break;
            Bitboard posMask = 1ULL << to;
            if (opponentBishops & posMask || opponentQueens & posMask) return true;
            if (whitePieces & posMask || blackPieces & posMask) break;
        }
    }

    // Check for rook/queen straight attacks
    const int rookDirections[4] = { 8, -8, 1, -1 };
    for (int dir : rookDirections) {
        int to = square;
        while (true) {
            to += dir;
            if (to < 0 || to >= 64 || (to % 8 == 0 && dir == 1) || (to % 8 == 7 && dir == -1)) break;
            Bitboard posMask = 1ULL << to;
            if (opponentRooks & posMask || opponentQueens & posMask) return true;
            if (whitePieces & posMask || blackPieces & posMask) break;
        }
    }

    // Check for king attacks
    const int kingMoves[8] = { 8, -8, 1, -1, 9, 7, -9, -7 };
    for (int move : kingMoves) {
        int to = square + move;
        if (to >= 0 && to < 64 && abs((square % 8) - (to % 8)) <= 1) {
            if (opponentKing & (1ULL << to)) return true;
        }
    }

    return false;
}

std::vector<Move> Board::generateQueenMoves(Bitboard queens, Bitboard ownPieces, Bitboard opponentPieces) {
    std::vector<Move> moves;
    std::vector<Move> bishopMoves = generateBishopMoves(queens, ownPieces, opponentPieces);
    std::vector<Move> rookMoves = generateRookMoves(queens, ownPieces, opponentPieces);

    moves.insert(moves.end(), bishopMoves.begin(), bishopMoves.end());
    moves.insert(moves.end(), rookMoves.begin(), rookMoves.end());
    return moves;
}

std::vector<Move> Board::generateAllMoves() {
    std::vector<Move> allMoves;

    Bitboard ownPieces = whiteToMove ? whitePieces : blackPieces;
    Bitboard opponentPieces = whiteToMove ? blackPieces : whitePieces;

    // Generate moves for all pieces
    std::vector<Move> pawnMoves = generatePawnMoves(whiteToMove ? whitePawns : blackPawns, ownPieces, opponentPieces);
    std::vector<Move> bishopMoves = generateBishopMoves(whiteToMove ? whiteBishops : blackBishops, ownPieces, opponentPieces);
    std::vector<Move> rookMoves = generateRookMoves(whiteToMove ? whiteRooks : blackRooks, ownPieces, opponentPieces);
    std::vector<Move> knightMoves = generateKnightMoves(whiteToMove ? whiteKnights : blackKnights, ownPieces, opponentPieces);
    std::vector<Move> kingMoves = generateKingMoves(whiteToMove ? whiteKing : blackKing, ownPieces, opponentPieces);
    std::vector<Move> queenMoves = generateQueenMoves(whiteToMove ? whiteQueens : blackQueens, ownPieces, opponentPieces);
   
    // Combine all generated moves into one vector
    allMoves.insert(allMoves.end(), pawnMoves.begin(), pawnMoves.end());
    allMoves.insert(allMoves.end(), bishopMoves.begin(), bishopMoves.end());
    allMoves.insert(allMoves.end(), rookMoves.begin(), rookMoves.end());
    allMoves.insert(allMoves.end(), knightMoves.begin(), knightMoves.end());
    allMoves.insert(allMoves.end(), kingMoves.begin(), kingMoves.end());
    allMoves.insert(allMoves.end(), queenMoves.begin(), queenMoves.end());
    // Filter out moves that put the king in check
    std::vector<Move> legalMoves;
    Bitboard store = enPassantTarget;
    bool whiteKingMovedStore = whiteKingMoved;
    bool whiteLRookMovedStore = whiteLRookMoved;
    bool whiteRRookMovedStore = whiteRRookMoved;
    bool blackKingMovedStore = blackKingMoved;
    bool blackLRookMovedStore = blackLRookMoved;
    bool blackRRookMovedStore = blackRRookMoved;
    for (Move& move : allMoves) {
        makeMove(move);
        if (!amIInCheck(!whiteToMove)) {
            legalMoves.push_back(move);
        }
        enPassantTarget = store;
        whiteKingMoved = whiteKingMovedStore;
        whiteLRookMoved = whiteLRookMovedStore;
        whiteRRookMoved = whiteRRookMovedStore;
        blackKingMoved = blackKingMovedStore;
        blackLRookMoved = blackLRookMovedStore;
        blackRRookMoved = blackRRookMovedStore;
        undoMove(move);

    }
    return legalMoves;
}

bool Board::amIInCheck(bool player) {
    Bitboard ownKing = player ? whiteKing : blackKing;
    Bitboard opponentPawns = player ? blackPawns : whitePawns;
    Bitboard opponentRooks = player ? blackRooks : whiteRooks;
    Bitboard opponentKnights = player ? blackKnights : whiteKnights;
    Bitboard opponentBishops = player ? blackBishops : whiteBishops;
    Bitboard opponentQueens = player ? blackQueens : whiteQueens;
    Bitboard opponentKing = player ? blackKing : whiteKing;
    Bitboard ownPieces = player ? whitePieces : blackPieces;
    Bitboard enemyPieces = player ? blackPieces : whitePieces;

    int kingPos = ctzll(ownKing);

    // Check for pawn attacks
    Bitboard pawnAttacks = player ? ((opponentPawns >> 7) & 0xFEFEFEFEFEFEFEFE) | ((opponentPawns >> 9) & 0x7F7F7F7F7F7F7F7F)
        : ((opponentPawns << 7) & 0x7F7F7F7F7F7F7F7F) | ((opponentPawns << 9) & 0xFEFEFEFEFEFEFEFE);


    if (pawnAttacks & ownKing) {
        return true;
    }

    // Check for knight attacks
    const int knightMoves[8] = { 17, 15, 10, 6, -17, -15, -10, -6 };
    for (int move : knightMoves) {
        int to = kingPos + move;
        if (to >= 0 && to < 64 && abs((kingPos % 8) - (to % 8)) <= 2) {
            if (opponentKnights & (1ULL << to)) return true;
        }
    }

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

void Board::makeMove(Move& move) {
    Bitboard fromMask = 1ULL << move.from;
    Bitboard toMask = 1ULL << move.to;
    Bitboard enPassantPrev = enPassantTarget;
    move.capturedPiece = 0; // Reset captured piece

    if (whiteToMove) {
        // Update piece bitboards for white
        if (whitePawns & fromMask) {
            whitePawns ^= fromMask | toMask;
            if (move.to == move.from + 16) {
                enPassantTarget = 1ULL << (move.from + 8);
            }
            else if (move.promotion) {
                whitePawns &= ~toMask;
                switch (move.promotion) {
                case 'q': whiteQueens |= toMask; break;
                case 'r': whiteRooks |= toMask; break;
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
        else if (whiteKnights & fromMask) {
            whiteKnights ^= fromMask | toMask;
        }
        else if (whiteBishops & fromMask) {
            whiteBishops ^= fromMask | toMask;
        }
        else if (whiteQueens & fromMask) {
            whiteQueens ^= fromMask | toMask;
        }
        else if (whiteKing & fromMask) {
            whiteKing ^= fromMask | toMask;
            whiteKingMoved = true;
            if (move.to == move.from - 2) {
                whiteRooks ^= 0x0000000000000005;
            }
            else if (move.to == move.from + 2) {
                whiteRooks ^= 0x0000000000000090;
            }
        }

        if (move.isCapture) {
            if (blackPawns & toMask) {
                move.capturedPiece = 'p';
                blackPawns &= ~toMask;
            }
            else if (blackRooks & toMask) {
                move.capturedPiece = 'r';
                blackRooks &= ~toMask;
            }
            else if (blackKnights & toMask) {
                move.capturedPiece = 'n';
                blackKnights &= ~toMask;
            }
            else if (blackBishops & toMask) {
                move.capturedPiece = 'b';
                blackBishops &= ~toMask;
            }
            else if (blackQueens & toMask) {
                move.capturedPiece = 'q';
                blackQueens &= ~toMask;
            }
            else if (blackKing & toMask) {
                move.capturedPiece = 'k';
                blackKing &= ~toMask;
            }
            else if (enPassantTarget & toMask) {
                blackPawns &= ~(toMask >> 8);
            }
        }
        whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
        blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;

    }
    else {
        // Update piece bitboards for black
        if (blackPawns & fromMask) {
            blackPawns ^= fromMask | toMask;
            if (move.to == move.from - 16) {
                enPassantTarget = 1ULL << (move.from - 8);
            }
            else if (move.promotion) {
                blackPawns &= ~toMask;
                switch (move.promotion) {
                case 'q': blackQueens |= toMask; break;
                case 'r': blackRooks |= toMask; break;
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
        else if (blackKnights & fromMask) {
            blackKnights ^= fromMask | toMask;
        }
        else if (blackBishops & fromMask) {
            blackBishops ^= fromMask | toMask;
        }
        else if (blackQueens & fromMask) {
            blackQueens ^= fromMask | toMask;
        }
        else if (blackKing & fromMask) {
            blackKing ^= fromMask | toMask;
            blackKingMoved = true;
            if (move.to == move.from - 2) {
                blackRooks ^= 0x0500000000000000;
            }
            else if (move.to == move.from + 2) {
                blackRooks ^= 0x9000000000000000;
            }
        }

        if (move.isCapture) {
            if (whitePawns & toMask) {
                move.capturedPiece = 'p';
                whitePawns &= ~toMask;
            }
            else if (whiteRooks & toMask) {
                move.capturedPiece = 'r';
                whiteRooks &= ~toMask;
            }
            else if (whiteKnights & toMask) {
                move.capturedPiece = 'n';
                whiteKnights &= ~toMask;
            }
            else if (whiteBishops & toMask) {
                move.capturedPiece = 'b';
                whiteBishops &= ~toMask;
            }
            else if (whiteQueens & toMask) {
                move.capturedPiece = 'q';
                whiteQueens &= ~toMask;
            }
            else if (whiteKing & toMask) {
                move.capturedPiece = 'k';
                whiteKing &= ~toMask;
            }
            else if (enPassantTarget & toMask) {
                whitePawns &= ~(toMask << 8);
            }
        }
        whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
        blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;
    }

    if (move.promotion) {
        if (whiteToMove) {
            whitePawns &= ~toMask;
            switch (move.promotion) {
            case 'q': whiteQueens |= toMask; break;
            case 'r': whiteRooks |= toMask; break;
            case 'b': whiteBishops |= toMask; break;
            case 'n': whiteKnights |= toMask; break;
            }
        }
        else {
            blackPawns &= ~toMask;
            switch (move.promotion) {
            case 'q': blackQueens |= toMask; break;
            case 'r': blackRooks |= toMask; break;
            case 'b': blackBishops |= toMask; break;
            case 'n': blackKnights |= toMask; break;
            }
        }
    }

    if (enPassantPrev == enPassantTarget) {
        enPassantTarget = 0x0;
    }

    whiteToMove = !whiteToMove;
}

void Board::undoMove(const Move& move) {
    Bitboard fromMask = 1ULL << move.from;
    Bitboard toMask = 1ULL << move.to;

    if (!whiteToMove) {
        // Handle undoing promotions
        if (move.promotion) {
            whitePawns |= fromMask;
            switch (move.promotion) {
            case 'q': whiteQueens &= ~toMask; break;
            case 'r': whiteRooks &= ~toMask; break;
            case 'b': whiteBishops &= ~toMask; break;
            case 'n': whiteKnights &= ~toMask; break;
            }
        }
        else if (whitePawns & toMask) {
            whitePawns ^= fromMask | toMask;
        }
        else if (whiteRooks & toMask) {
            whiteRooks ^= fromMask | toMask;
        }
        else if (whiteKnights & toMask) {
            whiteKnights ^= fromMask | toMask;
        }
        else if (whiteBishops & toMask) {
            whiteBishops ^= fromMask | toMask;
        }
        else if (whiteQueens & toMask) {
            whiteQueens ^= fromMask | toMask;
        }
        else if (whiteKing & toMask) {
            whiteKing ^= fromMask | toMask;
            if (move.to == move.from - 2) { // Kingside castling
                whiteRooks ^= 0x0000000000000005; // Move the rook
            }
            else if (move.to == move.from + 2) { // Queenside castling
                whiteRooks ^= 0x0000000000000090; // Move the rook
            }
        }

        // Handle captures
        if (move.isCapture) {
            // Restore the pawn for en passant captures
            if ((move.to == move.from + 9 || move.to == move.from + 7)) {
                if (toMask & enPassantTarget) {
                    blackPawns |= toMask >> 8;
                }
            }

            switch (move.capturedPiece) {
            case 'p': blackPawns |= toMask; break;
            case 'r': blackRooks |= toMask; break;
            case 'n': blackKnights |= toMask; break;
            case 'b': blackBishops |= toMask; break;
            case 'q': blackQueens |= toMask; break;
            case 'k': blackKing |= toMask; break;
            }
        }
    }
    else {
        // Handle undoing promotions
        if (move.promotion) {
            blackPawns |= fromMask;
            switch (move.promotion) {
            case 'q': blackQueens &= ~toMask; break;
            case 'r': blackRooks &= ~toMask; break;
            case 'b': blackBishops &= ~toMask; break;
            case 'n': blackKnights &= ~toMask; break;
            }
        }
        else if (blackPawns & toMask) {
            blackPawns ^= fromMask | toMask;
        }
        else if (blackRooks & toMask) {
            blackRooks ^= fromMask | toMask;
        }
        else if (blackKnights & toMask) {
            blackKnights ^= fromMask | toMask;
        }
        else if (blackBishops & toMask) {
            blackBishops ^= fromMask | toMask;
        }
        else if (blackQueens & toMask) {
            blackQueens ^= fromMask | toMask;
        }
        else if (blackKing & toMask) {
            blackKing ^= fromMask | toMask;
            if (move.to == move.from - 2) { // Kingside castling
                blackRooks ^= 0x0500000000000000; // Move the rook
            }
            else if (move.to == move.from + 2) { // Queenside castling
                blackRooks ^= 0x9000000000000000; // Move the rook
            }
        }

        // Handle captures
        if (move.isCapture) {
            // Restore the pawn for en passant captures
            if ((move.to == move.from - 9 || move.to == move.from - 7)) {
                if (toMask & enPassantTarget) {
                    whitePawns |= toMask << 8;
                }
            }

            switch (move.capturedPiece) {
            case 'p': whitePawns |= toMask; break;
            case 'r': whiteRooks |= toMask; break;
            case 'n': whiteKnights |= toMask; break;
            case 'b': whiteBishops |= toMask; break;
            case 'q': whiteQueens |= toMask; break;
            case 'k': whiteKing |= toMask; break;
            }
        }
    }
    whitePieces = whitePawns | whiteRooks | whiteKnights | whiteBishops | whiteQueens | whiteKing;
    blackPieces = blackPawns | blackRooks | blackKnights | blackBishops | blackQueens | blackKing;
    whiteToMove = !whiteToMove; // Toggle the side to move
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
        else if (isdigit(c)) {
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
    case 'P': return 0;
    case 'N': return 1;
    case 'B': return 2;
    case 'R': return 3;
    case 'Q': return 4;
    case 'K': return 5;
    case 'p': return 6;
    case 'n': return 7;
    case 'b': return 8;
    case 'r': return 9;
    case 'q': return 10;
    case 'k': return 11;
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
        positionHistory[hash]++;
    }
    else {
        positionHistory[hash]--;
    }
}

bool Board::isThreefoldRepetition(bool print) {
    uint64_t hash = generateZobristHash();
    if (print){
            std::cout << positionHistory[hash] << std::endl;

    }
    return positionHistory[hash] >= 3;
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
        return 3;
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

bool isGoodCapture(const Move& move, const Board& board) {
    char attackerPiece = board.getPieceAt(move.from);
    char defenderPiece = move.capturedPiece;

    int attackerValue = getPieceValue(attackerPiece);
    int defenderValue = getPieceValue(defenderPiece);

    return defenderValue > attackerValue;
}

bool isEqualCapture(const Move& move, const Board& board) {
    char attackerPiece = board.getPieceAt(move.from);
    char defenderPiece = move.capturedPiece;

    int attackerValue = getPieceValue(attackerPiece);
    int defenderValue = getPieceValue(defenderPiece);

    return defenderValue == attackerValue;
}

std::vector<Move> orderMoves(Board & board, const std::vector<Move>&moves, TT_Entry * ttEntry, int depth) {
    Move hashMove = (ttEntry && ttEntry->depth >= depth) ? ttEntry->move : NO_MOVE;
    Move shallowHashMove = (ttEntry && ttEntry->depth >= (depth - 2)) ? ttEntry->move : NO_MOVE;
    std::vector<Move> orderedMoves;
    std::vector<Move> hashMoves;
    std::vector<Move> capturesAndPromotions;
    std::vector<Move> promotions;
    std::vector<Move> goodCaptures;
    std::vector<Move> equalCaptures;
    std::vector<Move> nonCaptures;
    std::vector<Move> losingCaptures;

    for (const Move& move : moves) {
        // Assuming you have a function isIterativeDeepeningMove to check if the move is an iterative deepening move
        if (move == hashMove) {
            hashMoves.push_back(move);
        }
        else if (move == shallowHashMove) {
            hashMoves.push_back(move);
        }
        else if (move.isCapture || move.promotion) {
            capturesAndPromotions.push_back(move);
        }
        else if (move.promotion) {
            promotions.push_back(move);
        }
        else if (isGoodCapture(move, board)) {
            goodCaptures.push_back(move);
        }
        else if (isEqualCapture(move, board)) {
            equalCaptures.push_back(move);
        }
        else if (!move.isCapture) {
            nonCaptures.push_back(move);
        }
        else {
            losingCaptures.push_back(move);
        }
    }

    // Append moves in the order of priority
    orderedMoves.insert(orderedMoves.end(), hashMoves.begin(), hashMoves.end());
    orderedMoves.insert(orderedMoves.end(), capturesAndPromotions.begin(), capturesAndPromotions.end());
    orderedMoves.insert(orderedMoves.end(), promotions.begin(), promotions.end());
    orderedMoves.insert(orderedMoves.end(), goodCaptures.begin(), goodCaptures.end());
    orderedMoves.insert(orderedMoves.end(), equalCaptures.begin(), equalCaptures.end());
    orderedMoves.insert(orderedMoves.end(), nonCaptures.begin(), nonCaptures.end());
    orderedMoves.insert(orderedMoves.end(), losingCaptures.begin(), losingCaptures.end());

    return orderedMoves;
}

bool isTacticalPosition(std::vector<Move> moves, Board board) {
    for (const Move& move : moves) {
        if (move.isCapture && (isGoodCapture(move, board) || isEqualCapture(move, board)) || move.promotion) {
            return true;
        }
    }
    return false;
}


void Board::resize_tt(uint64_t mb) {
    size_t entries = (mb * 1048576ull) / sizeof(TT_Entry);
    size_t new_entries = 1ull << (int)std::log2(entries);  // Ensures power of 2 size for efficient indexing
    transposition_table.resize(new_entries);
    clear_tt();  // Clear the table to ensure all entries are reset after resizing
}

// This function might be called from a GUI configuration dialog or command line option
void Board::configureTranspositionTableSize(uint64_t sizeInMB) {
    resize_tt(sizeInMB);
}

void Board::record_tt_entry(uint64_t hash_key, int score, TTFlag flag, Move move, int depth, int evaluation) {
    TT_Entry& tt_entry = transposition_table[hash_key & (transposition_table.size() - 1)];

    // Use deeper or exact information to replace existing entry
    if (tt_entry.key != hash_key || depth > tt_entry.depth || flag == HASH_FLAG_EXACT) {
        tt_entry.key = hash_key;
        tt_entry.score = score;
        tt_entry.flag = flag;
        tt_entry.move = move;
        tt_entry.depth = depth;
        tt_entry.evaluation = evaluation;
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

bool isNullViable(Board& board) {
    if (board.whiteToMove) {
        if ((std::_Popcount(board.whiteBishops) + std::_Popcount(board.whiteKnights) + std::_Popcount(board.whiteRooks) + (std::_Popcount(board.whiteQueens) * 2)) >= 2) {
            return true;
        }
        else {
            return false;
        }
    }
    else {
        if ((std::_Popcount(board.blackBishops) + std::_Popcount(board.blackKnights) + std::_Popcount(board.blackRooks) + (std::_Popcount(board.blackQueens) * 2)) >= 2) {
            return true;
        }
        else {
            return false;
        }
    }
}

