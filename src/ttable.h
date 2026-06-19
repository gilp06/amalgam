#pragma once

#include <cstdint>
#include <optional>
#include <chess.hpp>

class TTable {
    public:
        enum Bound {
            NONE,
            EXACT,
            LOWER,
            UPPER
        };
        struct Entry {
            uint64_t key;
            int16_t score;
            int16_t depth;
            Bound bd;
            uint16_t best_move;
        };

    private:
        std::vector<Entry> table;
        size_t mask;
    public:
        inline explicit TTable(size_t megabytes) {
            size_t bytes = megabytes * 1024 * 1024;
            size_t entry_count = bytes / sizeof(Entry);

            // set table length to be power of two
            size_t actual_size = 1;
            while (actual_size <= entry_count / 2) {
                actual_size <<= 1;
            }

            table.resize(actual_size);
            mask = actual_size - 1;
        }

        inline void clear() {
            std::fill(table.begin(), table.end(), Entry{0,0,0,Bound::NONE,0});
        }

        inline void store(uint64_t key, int16_t score, int16_t depth, Bound bd, uint16_t best_move) {
            size_t index = key & mask;

            table[index] = Entry{key, score, depth, bd, best_move};
        }

        inline void store(const Entry& e) {
            size_t index = e.key & mask;

            table[index] = e;
        }

        inline std::optional<Entry> probe(uint64_t key) {
            size_t index = key & mask;

            const Entry& entry = table[index];
            if (entry.key == key) {
                return entry;
            }
            return std::nullopt;
        }
        inline void resize(size_t megabytes) {
            size_t bytes = megabytes * 1024 * 1024;
            size_t entry_count = bytes / sizeof(Entry);

            size_t actual_size = 1;
            while (actual_size <= entry_count / 2) {
                actual_size <<= 1;
            }

            table.resize(actual_size);
            mask = actual_size - 1;

            clear();
        }
};
