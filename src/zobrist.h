#pragma once
#ifndef ZOBRIST_H
#define ZOBRIST_H

#include <cstdint>

const int NUM_PIECES = 12; // 6 pieces * 2 colors
const int NUM_SQUARES = 64;
const int NUM_CASTLING_RIGHTS = 6;
const int NUM_EN_PASSANT_FILES = 8;

extern uint64_t zobristTable[NUM_PIECES][NUM_SQUARES];
extern uint64_t zobristCastling[NUM_CASTLING_RIGHTS];
extern uint64_t zobristEnPassant[NUM_EN_PASSANT_FILES];
extern uint64_t zobristSideToMove;

void initializeZobristTable();

#endif // ZOBRIST_H
