#pragma once
// Phase 0 placeholder system: "wanderers".
//
// A small band of people who walk between camps, meet, have children, live
// through hard winters and die. It exists only to exercise the whole
// pipeline (agenda, random numbers, events with causes, the entity
// inspector, snapshots, save/load). It is removed once real systems exist.

#include <string>
#include <vector>

#include "hsim/agenda.hpp"
#include "hsim/ids.hpp"
#include "hsim/world.hpp"

namespace hsim {

struct WandererTag {
    static constexpr EntityKind kind = EntityKind::Wanderer;
};
using WandererId = Id<WandererTag>;

class WanderersSystem final : public System {
public:
    // Event type codes (placeholder range 60000+). Stable: saved in history.
    enum : EventType {
        kArrival = 60000,
        kJourney = 60001,
        kMeeting = 60002,
        kBirth = 60003,
        kDeath = 60004,
        kHardWinter = 60005,
    };
    enum : Role {
        kRoleWanderer = 60000,
        kRoleOther = 60001,
        kRoleParent = 60002,
        kRoleChild = 60003,
        kRoleDeceased = 60004,
    };
    enum : std::uint16_t {
        kCondAge = 60000,
        kCondCold = 60001,
    };

    static constexpr int kCamps = 8;

    const char* name() const override { return "wanderers"; }
    const char* save_tag() const override { return "WAND"; }
    Cadence cadence() const override { return Cadence::Daily; }

    void register_catalog(EventCatalog& catalog) override;
    void init(World& world) override;
    void update(World& world) override;
    void save(BinaryWriter& w) const override;
    void load(BinaryReader& r) override;

    bool owns(EntityKind k) const override { return k == EntityKind::Wanderer; }
    std::string name_of(EntityRef ref) const override;
    void list(EntityKind, bool alive_only, std::vector<EntityRef>& out) const override;
    void inspect(EntityRef ref, const World& world, std::vector<InspectRow>& rows) const override;
    void snapshot_layers(std::vector<SnapshotLayer>& layers) const override;

    std::size_t alive_count() const;

private:
    enum : std::uint16_t { kActNext = 1 };

    WandererId spawn(World& world, std::string name, Day born, int camp, WandererId mother, WandererId father);
    void act(World& world, WandererId id);
    void die(World& world, WandererId id, Cause cause);
    void hard_winters(World& world);
    void book_next(World& world, WandererId id);
    bool alive(std::uint32_t i) const { return died_[i] == kAlive; }
    std::string make_name(const Rng& rng) const;

    static constexpr Day kAlive = INT64_MIN;

    IdAllocator<WandererTag> ids_;
    // Per-wanderer columns, indexed by id.index. Wanderers are never
    // destroyed: the dead stay in the tables as part of history.
    std::vector<std::string> names_;
    std::vector<Day> born_, died_, lifespan_days_;
    std::vector<std::int32_t> camp_;
    std::vector<WandererId> mother_, father_;
    std::vector<EventId> last_event_;
    std::vector<std::uint32_t> living_;  // indices of the living, ascending

    std::vector<std::string> camp_names_;
    Agenda agenda_;
};

}  // namespace hsim
