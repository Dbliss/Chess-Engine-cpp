#pragma once

#include "chess.h"
#include <tuple>
#include <iostream>
#include <chrono>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>


std::tuple<Move, double_t> engine2Start(Board& board, int depth, std::vector<std::tuple<Move, double_t>>& iterativeDeepeningMoves, double_t alpha, double_t beta);
std::tuple<Move, double_t> engine2(Board& board, int depth, double_t alpha, double_t beta, int startDepth, std::vector<std::tuple<Move, double_t>>& iterativeDeepeningMoves, int totalExtensions, bool lastIterationNull);
extern std::chrono::time_point<std::chrono::high_resolution_clock> endTime2;
bool isEndgameDraw(int numWhiteBishops, int numWhiteKnights, int numBlackKnights, int numBlackBishops);
void ponderEngine2(Board& board);
Move getEngineMove2(Board& board, int timeLimit);
extern int iterativeDeepeningCallCount2;
extern bool isPonderingRunning;
extern std::atomic<bool> isPaused;


