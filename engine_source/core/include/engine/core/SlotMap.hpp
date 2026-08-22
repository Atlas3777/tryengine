#pragma once

#include <EASTL/vector.h>

namespace tryengine::core {

template <typename T>
class SlotMap {
public:
    struct Key {
        uint32_t index = 0;
        uint32_t generation = 0;

        Key() = default;
        Key(uint32_t idx, uint32_t gen) : index(idx), generation(gen) {}

        // 1. Автоматическая распаковка из uint64_t
        Key(uint64_t raw)
            : index(static_cast<uint32_t>(raw >> 32)),
              generation(static_cast<uint32_t>(raw & 0xFFFFFFFF)) {}

        // 2. Автоматическая упаковка в uint64_t
        operator uint64_t() const {
            return (static_cast<uint64_t>(index) << 32) | static_cast<uint64_t>(generation);
        }

        explicit operator bool() const { return generation != 0; }
        bool operator==(const Key& rhs) const { return index == rhs.index && generation == rhs.generation; }
        bool operator!=(const Key& rhs) const { return !(*this == rhs); }
    };

private:
    struct Slot {
        uint32_t dense_index = 0;
        uint32_t generation = 1;
    };

    eastl::vector<T> m_dense;
    eastl::vector<uint32_t> m_dense_to_slot;
    eastl::vector<Slot> m_slots;
    eastl::vector<uint32_t> m_free_slots;

public:
    SlotMap() = default;

    template <typename... Args>
    Key emplace(Args&&... args) {
        uint32_t slot_idx = 0;

        if (!m_free_slots.empty()) {
            slot_idx = m_free_slots.back();
            m_free_slots.pop_back();
        } else {
            slot_idx = static_cast<uint32_t>(m_slots.size());
            m_slots.emplace_back();
        }

        auto& slot = m_slots[slot_idx];
        slot.dense_index = static_cast<uint32_t>(m_dense.size());

        m_dense.emplace_back(eastl::forward<Args>(args)...);
        m_dense_to_slot.push_back(slot_idx);

        return Key{slot_idx, slot.generation};
    }

    Key insert(const T& value) { return emplace(value); }
    Key insert(T&& value) { return emplace(eastl::move(value)); }

    bool contains(Key key) const {
        if (key.index >= m_slots.size())
            return false;
        return m_slots[key.index].generation == key.generation;
    }

    T* get(Key key) {
        if (!contains(key))
            return nullptr;
        return &m_dense[m_slots[key.index].dense_index];
    }

    const T* get(Key key) const {
        if (!contains(key))
            return nullptr;
        return &m_dense[m_slots[key.index].dense_index];
    }

    bool erase(Key key) {
        if (!contains(key))
            return false;

        auto& slot = m_slots[key.index];
        const uint32_t removed_dense_idx = slot.dense_index;
        const uint32_t last_dense_idx = static_cast<uint32_t>(m_dense.size() - 1);

        if (removed_dense_idx != last_dense_idx) {
            m_dense[removed_dense_idx] = eastl::move(m_dense[last_dense_idx]);

            const uint32_t moved_slot_idx = m_dense_to_slot[last_dense_idx];
            m_slots[moved_slot_idx].dense_index = removed_dense_idx;
            m_dense_to_slot[removed_dense_idx] = moved_slot_idx;
        }

        m_dense.pop_back();
        m_dense_to_slot.pop_back();

        slot.generation++;
        if (slot.generation == 0)
            slot.generation = 1;
        m_free_slots.push_back(key.index);

        return true;
    }

    void clear() {
        m_dense.clear();
        m_dense_to_slot.clear();
        m_slots.clear();
        m_free_slots.clear();
    }

    T* data() { return m_dense.data(); }
    const T* data() const { return m_dense.data(); }
    size_t size() const { return m_dense.size(); }
    bool empty() const { return m_dense.empty(); }

    auto begin() { return m_dense.begin(); }
    auto end() { return m_dense.end(); }
    auto begin() const { return m_dense.begin(); }
    auto end() const { return m_dense.end(); }
};
}  // namespace tryengine::core