#pragma once

#include "chess.h"
#include <tuple>
#include <iostream>
#include <chrono>

std::tuple<Move, double_t> perft2(Board& board, int depth, std::vector<std::tuple<Move, double_t>>& iterativeDeepeningMoves);
std::tuple<Move, double_t> perftHelper2(Board& board, int depth, double_t alpha, double_t beta, int startDepth, std::vector<std::tuple<Move, double_t>>& iterativeDeepeningMoves, int totalExtensions, bool lastIterationNull);