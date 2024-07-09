#include "Zobrist.h"
#include <random>
#include "iostream"
#include <array>
#include <vector>
#include <unordered_set>
#include <random>

uint64_t zobristTable[NUM_PIECES][NUM_SQUARES];
uint64_t zobristCastling[NUM_CASTLING_RIGHTS];
uint64_t zobristEnPassant[NUM_EN_PASSANT_FILES];
uint64_t zobristSideToMove;


// Function to generate random numbers
std::vector<uint64_t> generateRandomNumbers(size_t count, uint64_t seed)
{
    std::unordered_set<uint64_t> randomNumbers;
    std::mt19937_64 eng(seed);
    std::uniform_int_distribution<uint64_t> distr(1, UINT64_MAX - 1);

    while (randomNumbers.size() < count)
    {
        randomNumbers.insert(distr(eng));
    }

    return std::vector<uint64_t>(randomNumbers.begin(), randomNumbers.end());
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