#pragma once

#include "chess.h"
#include <tuple>
#include <iostream>
#include <chrono>

int perft(Board& board, int depth, int startDepth);
int perftHelper(Board& board, int depth, int startDepth);

std::tuple<Move, double_t> engine(Board& board, int depth, std::vector<std::tuple<Move, double_t>>& iterativeDeepeningMoves, double_t alpha, double_t beta);
std::tuple<Move, double_t> engineHelper(Board& board, int depth, double_t alpha, double_t beta, int startDepth, std::vector<std::tuple<Move, double_t>>& iterativeDeepeningMoves, int totalExtensions, bool lastIterationNull);
std::vector<Move> generateCaptures(Board& board, std::vector<Move> allMoves);
double_t evaluate(Board& board);

extern std::chrono::time_point<std::chrono::high_resolution_clock> endTime;