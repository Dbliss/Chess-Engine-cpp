#include "Zobrist.h"
#include <random>
#include "iostream"
#include <array>
#include <vector>
#include <unordered_set>
#include <random>
#include <limits>

uint64_t zobristTable[NUM_PIECES][NUM_SQUARES];
uint64_t zobristCastling[NUM_CASTLING_RIGHTS];
uint64_t zobristEnPassant[NUM_EN_PASSANT_FILES];
uint64_t zobristSideToMove;

std::vector<uint64_t> generateRandomNumbers(size_t count, uint64_t seed)
{
    std::vector<uint64_t> nums;
    nums.reserve(count);

    std::unordered_set<uint64_t> seen;
    seen.reserve(count * 2);

    std::mt19937_64 eng(seed);
    std::uniform_int_distribution<uint64_t> dist(
        1ULL, std::numeric_limits<uint64_t>::max()
    );

    while (nums.size() < count) {
        uint64_t x = dist(eng);
        if (seen.insert(x).second) {
            nums.push_back(x);
        }
    }
    return nums;
}


void initializeZobristTable() {
    // Generate random numbers
    const size_t totalNumbers = 64 * 12 + 1 + 6 + 8;  // all pieces + moves , whitetomove, castling, en passant column
    auto randomNumbers = generateRandomNumbers(totalNumbers, 5259408);
    int j = 0;
    for (int piece = 0; piece < NUM_PIECES; ++piece) {
        for (int square = 0; square < NUM_SQUARES; ++square) {
            zobristTable[piece][square] = randomNumbers[j];
            j += 1;
        }
    }

    for (int i = 0; i < NUM_CASTLING_RIGHTS; ++i) {
        zobristCastling[i] = randomNumbers[j];
        j += 1;
    }

    for (int i = 0; i < NUM_EN_PASSANT_FILES; ++i) {
        zobristEnPassant[i] = randomNumbers[j];
        j += 1;
    }

    zobristSideToMove = randomNumbers[j];
    j += 1;
}