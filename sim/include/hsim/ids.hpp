#pragma once
// Stable, typed entity IDs.
//
// An ID is (index, generation). When an entity is removed its slot can be
// reused, but the generation is bumped, so old IDs never silently point at a
// new entity. A dead person's ID stays valid as a key into the history.

#include <cstdint>
#include <functional>
#include <vector>

namespace hsim {

// Every kind of entity the simulation knows about. Values are stored in saves
// and history files: append new kinds, never renumber.
enum class EntityKind : std::uint8_t {
    None = 0,
    Wanderer = 1,  // Phase 0 placeholder entity (removed once real systems exist)
    Cell = 2,
    Cohort = 3,
    Person = 4,
    Org = 5,
};

const char* entity_kind_name(EntityKind kind);

template <class Tag>
struct Id {
    static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

    std::uint32_t index = kInvalidIndex;
    std::uint32_t generation = 0;

    constexpr bool valid() const { return index != kInvalidIndex; }
    friend constexpr bool operator==(Id, Id) = default;
    friend constexpr auto operator<=>(Id, Id) = default;
};

// A reference to any entity, used where kinds mix (event participants).
struct EntityRef {
    EntityKind kind = EntityKind::None;
    std::uint32_t index = 0xFFFFFFFFu;
    std::uint32_t generation = 0;

    constexpr bool valid() const { return kind != EntityKind::None; }
    friend constexpr bool operator==(EntityRef, EntityRef) = default;
    friend constexpr auto operator<=>(EntityRef, EntityRef) = default;

    std::uint64_t key() const {
        return (std::uint64_t(kind) << 56) ^ (std::uint64_t(generation & 0xFFFFFFu) << 32) ^ index;
    }
};

// Each entity type declares a tag with a static `kind`.
template <class Tag>
constexpr EntityRef ref(Id<Tag> id) {
    return EntityRef{Tag::kind, id.index, id.generation};
}

// Allocates IDs and tracks which are alive. Component data lives in parallel
// arrays owned by the system that owns the entity type, indexed by `index`.
template <class Tag>
class IdAllocator {
public:
    using IdType = Id<Tag>;

    IdType create() {
        if (!free_.empty()) {
            std::uint32_t index = free_.back();
            free_.pop_back();
            alive_[index] = 1;
            ++alive_count_;
            return IdType{index, generations_[index]};
        }
        auto index = static_cast<std::uint32_t>(generations_.size());
        generations_.push_back(0);
        alive_.push_back(1);
        ++alive_count_;
        return IdType{index, 0};
    }

    // Removes the entity. Its slot may be reused later with a new generation.
    void destroy(IdType id) {
        if (!alive(id)) return;
        alive_[id.index] = 0;
        ++generations_[id.index];
        free_.push_back(id.index);
        --alive_count_;
    }

    bool alive(IdType id) const {
        return id.index < generations_.size() && alive_[id.index] && generations_[id.index] == id.generation;
    }

    // Number of slots ever allocated; component arrays must be at least this long.
    std::uint32_t capacity() const { return static_cast<std::uint32_t>(generations_.size()); }
    std::uint32_t alive_count() const { return alive_count_; }

    template <class F>
    void for_each_alive(F&& f) const {
        for (std::uint32_t i = 0; i < capacity(); ++i)
            if (alive_[i]) f(IdType{i, generations_[i]});
    }

    // Raw state, for save/load and hashing.
    std::vector<std::uint32_t> generations_;
    std::vector<std::uint8_t> alive_;
    std::vector<std::uint32_t> free_;
    std::uint32_t alive_count_ = 0;
};

}  // namespace hsim
