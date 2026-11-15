#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
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
        struct Slot {
            std::atomic<uint64_t> sequence{0};
            std::atomic<uint64_t> key{0};
            std::atomic<uint64_t> data{0};
        };

        std::unique_ptr<Slot[]> table;
        size_t table_size;
        size_t mask;

        static uint64_t pack(const Entry& entry) {
            uint16_t score_bits;
            std::memcpy(&score_bits, &entry.score, sizeof(score_bits));
            return score_bits |
                   (static_cast<uint64_t>(
                        static_cast<uint16_t>(entry.depth)) << 16) |
                   (static_cast<uint64_t>(entry.bd) << 32) |
                   (static_cast<uint64_t>(entry.best_move) << 34);
        }

        static Entry unpack(uint64_t key, uint64_t data) {
            uint16_t score_bits = static_cast<uint16_t>(data);
            int16_t score;
            std::memcpy(&score, &score_bits, sizeof(score));
            return Entry{
                key,
                score,
                static_cast<int16_t>((data >> 16) & 0xffff),
                static_cast<Bound>((data >> 32) & 0x3),
                static_cast<uint16_t>((data >> 34) & 0xffff)
            };
        }

        static size_t calculateSize(size_t megabytes) {
            size_t bytes = megabytes * 1024 * 1024;
            size_t entry_count = bytes / sizeof(Slot);

            size_t actual_size = 1;
            while (actual_size <= entry_count / 2) {
                actual_size <<= 1;
            }
            return actual_size;
        }

        static void storeSlot(Slot& slot, const Entry& entry) {
            uint64_t sequence = slot.sequence.load();
            while (true) {
                if (sequence & 1) {
                    sequence = slot.sequence.load();
                    continue;
                }
                if (slot.sequence.compare_exchange_weak(
                        sequence, sequence + 1)) {
                    break;
                }
            }

            slot.key.store(entry.key);
            slot.data.store(pack(entry));
            slot.sequence.store(sequence + 2);
        }

    public:
        inline explicit TTable(size_t megabytes)
            : table_size(calculateSize(megabytes)), mask(table_size - 1) {
            table = std::make_unique<Slot[]>(table_size);
        }

        inline void clear() {
            Entry empty{0, 0, 0, Bound::NONE, 0};
            for (size_t i = 0; i < table_size; ++i) {
                storeSlot(table[i], empty);
            }
        }

        inline void store(uint64_t key, int16_t score, int16_t depth, Bound bd,
                          uint16_t best_move) {
            store(Entry{key, score, depth, bd, best_move});
        }

        inline void store(const Entry& entry) {
            size_t index = entry.key & mask;
            storeSlot(table[index], entry);
        }

        inline std::optional<Entry> probe(uint64_t key) const {
            size_t index = key & mask;
            const Slot& slot = table[index];

            while (true) {
                uint64_t before = slot.sequence.load();
                if (before & 1) {
                    continue;
                }

                uint64_t entry_key = slot.key.load();
                uint64_t data = slot.data.load();
                uint64_t after = slot.sequence.load();

                if (before == after) {
                    if (entry_key == key) {
                        return unpack(entry_key, data);
                    }
                    return std::nullopt;
                }
            }
        }

        inline void resize(size_t megabytes) {
            size_t new_size = calculateSize(megabytes);
            auto new_table = std::make_unique<Slot[]>(new_size);
            table = std::move(new_table);
            table_size = new_size;
            mask = new_size - 1;
        }
};
