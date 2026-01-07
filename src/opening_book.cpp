#include "opening_book.h"
#include <fstream>
#include <random>

bool OpeningBook::load(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    while (true) {
        uint64_t key;
        uint16_t count;

        in.read((char*)&key, sizeof(key));
        if (!in) break;
        in.read((char*)&count, sizeof(count));

        auto& vec = book_[key];
        vec.reserve(count);

        for (int i = 0; i < count; ++i) {
            uint16_t from, to, promo;
            uint32_t weight;

            in.read((char*)&from, sizeof(from));
            in.read((char*)&to, sizeof(to));
            in.read((char*)&promo, sizeof(promo));
            in.read((char*)&weight, sizeof(weight));
            Move move;
            move.from = from;
            move.to = to;
            move.promotion = promo;
            vec.push_back({ move, weight });
        }
    }
    return true;
}

bool OpeningBook::probe(uint64_t zobrist, Move& outMove) const {
    auto it = book_.find(zobrist);
    if (it == book_.end()) return false;

    const auto& moves = it->second;
    uint32_t total = 0;
    for (const auto& m : moves) total += m.weight;

    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<uint32_t> dist(1, total);

    uint32_t r = dist(rng);
    uint32_t acc = 0;

    for (const auto& m : moves) {
        acc += m.weight;
        if (r <= acc) {
            outMove = m.move;
            return true;
        }
    }
    return false;
}
