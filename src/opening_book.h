#pragma once
#include "chess.h"
#include <unordered_map>
#include <vector>

struct BookMove {
    Move move;
    uint32_t weight;
};

class OpeningBook {
public:
    bool load(const char* path);
    bool probe(uint64_t zobrist, Move& outMove) const;

private:
    std::unordered_map<uint64_t, std::vector<BookMove>> book_;
};
