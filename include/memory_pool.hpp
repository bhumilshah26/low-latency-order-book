#pragma once
#include <cstddef>
#include <new>
#include <utility>
#include <vector>

namespace ob {

// Fixed-capacity object pool with an intrusive free list.
//
// All memory is reserved once, up front. After that, allocate() and
// deallocate() are O(1) pointer swaps: no malloc, no system calls, no page
// faults, no locks. Freed slots are reused LIFO, so the next allocation gets
// the most recently touched (cache-hot) memory.
//
// Not thread-safe: intended for a single-threaded matching engine.
template <typename T>
class MemoryPool {
    // A slot is either free (holds a pointer to the next free slot) or in use
    // (holds a T). The two never coexist, so they share the same bytes.
    union Slot {
        Slot* next;
        alignas(T) std::byte storage[sizeof(T)];
    };

public:
    explicit MemoryPool(std::size_t capacity) : slots_(capacity) {
        // Thread every slot onto the free list. Writing to each slot here
        // also pre-faults the pages, so the hot path never page-faults.
        for (std::size_t i = 0; i + 1 < capacity; ++i) slots_[i].next = &slots_[i + 1];
        if (capacity > 0) {
            slots_[capacity - 1].next = nullptr;
            head_ = &slots_[0];
        }
    }

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    // Constructs a T in a free slot. Returns nullptr if the pool is full.
    template <typename... Args>
    T* allocate(Args&&... args) {
        if (head_ == nullptr) return nullptr;
        Slot* slot = head_;
        head_ = slot->next;
        ++in_use_;
        return ::new (static_cast<void*>(slot->storage)) T(std::forward<Args>(args)...);
    }

    // Destroys the object and pushes its slot back onto the free list.
    void deallocate(T* obj) noexcept {
        obj->~T();
        Slot* slot = reinterpret_cast<Slot*>(obj);  // storage is at offset 0 of Slot
        slot->next = head_;
        head_ = slot;
        --in_use_;
    }

    std::size_t capacity() const noexcept { return slots_.size(); }
    std::size_t in_use() const noexcept { return in_use_; }

private:
    std::vector<Slot> slots_;
    Slot* head_ = nullptr;
    std::size_t in_use_ = 0;
};

// Baseline with the same interface using plain new/delete. Only used to
// benchmark the order book with and without the pool.
template <typename T>
class HeapAllocator {
public:
    explicit HeapAllocator(std::size_t capacity) : capacity_(capacity) {}

    template <typename... Args>
    T* allocate(Args&&... args) {
        if (in_use_ == capacity_) return nullptr;
        ++in_use_;
        return new T(std::forward<Args>(args)...);
    }

    void deallocate(T* obj) noexcept {
        delete obj;
        --in_use_;
    }

    std::size_t capacity() const noexcept { return capacity_; }
    std::size_t in_use() const noexcept { return in_use_; }

private:
    std::size_t capacity_;
    std::size_t in_use_ = 0;
};

}  // namespace ob
