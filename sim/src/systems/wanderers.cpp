#include "hsim/systems/wanderers.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace hsim {

namespace {
constexpr double kDaysPerYear = 365.2425;
constexpr std::size_t kPopulationCap = 250;

const char* kOnsets[] = {"b", "d", "g", "k", "l", "m", "n", "r", "s", "t", "v", "z", "sh", "th", "kh", ""};
const char* kVowels[] = {"a", "e", "i", "o", "u", "aa", "ei", "ou"};
const char* kCodas[] = {"", "", "n", "r", "l", "k", "m", "sh"};

void put_id(BinaryWriter& w, WandererId id) {
    w.put(id.index);
    w.put(id.generation);
}
WandererId get_id(BinaryReader& r) {
    WandererId id;
    id.index = r.get<std::uint32_t>();
    id.generation = r.get<std::uint32_t>();
    return id;
}
}  // namespace

void WanderersSystem::register_catalog(EventCatalog& c) {
    c.add_role(kRoleWanderer, "wanderer");
    c.add_role(kRoleOther, "other");
    c.add_role(kRoleParent, "parent");
    c.add_role(kRoleChild, "child");
    c.add_role(kRoleDeceased, "deceased");
    c.add_type({kArrival, "Arrival", "{wanderer} arrived at {camp}", {"camp"}});
    c.add_type({kJourney, "Journey", "{wanderer} walked from {from} to {to}", {"from", "to"}});
    c.add_type({kMeeting, "Meeting", "{wanderer} met {other} at {camp}", {"camp"}});
    c.add_type({kBirth, "Birth", "{child} was born to {parent} at {camp}", {"camp"}});
    c.add_type({kDeath, "Death", "{deceased} died at {camp}, aged {age}", {"camp", "age"}});
    c.add_type({kHardWinter, "Hard winter", "A hard winter struck {camp}", {"camp", "severity"}});
    c.add_condition({kCondAge, "old age", "old age ({int} years)"});
    c.add_condition({kCondCold, "cold", "bitter cold (severity {value})"});
}

std::string WanderersSystem::make_name(const Rng& rng) const {
    int syllables = static_cast<int>(rng.range(2, 3, 0));
    std::string s;
    for (int i = 0; i < syllables; ++i) {
        s += kOnsets[rng.range(0, std::size(kOnsets) - 1, 1 + i * 3)];
        s += kVowels[rng.range(0, std::size(kVowels) - 1, 2 + i * 3)];
        if (i + 1 == syllables) s += kCodas[rng.range(0, std::size(kCodas) - 1, 3 + i * 3)];
    }
    if (!s.empty()) s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

WandererId WanderersSystem::spawn(World& world, std::string name, Day born, int camp, WandererId mother,
                                  WandererId father) {
    WandererId id = ids_.create();
    std::size_t n = ids_.capacity();
    names_.resize(n);
    born_.resize(n);
    died_.resize(n, kAlive);
    lifespan_days_.resize(n);
    camp_.resize(n);
    mother_.resize(n);
    father_.resize(n);
    last_event_.resize(n, kNoEvent);

    Rng rng = world.rng("wanderers").with(id.index).with("lifespan");
    double years = std::clamp(rng.normal(55.0, 15.0), 1.0, 95.0);

    names_[id.index] = std::move(name);
    born_[id.index] = born;
    died_[id.index] = kAlive;
    lifespan_days_[id.index] = static_cast<Day>(years * kDaysPerYear);
    camp_[id.index] = camp;
    mother_[id.index] = mother;
    father_[id.index] = father;
    living_.push_back(id.index);  // indices only grow, so living_ stays sorted
    return id;
}

void WanderersSystem::init(World& world) {
    Rng rng = world.rng("wanderers").with("init");
    camp_names_.clear();
    for (int c = 0; c < kCamps; ++c) camp_names_.push_back(make_name(rng.with("camp").with(c)) + " camp");

    for (int i = 0; i < 60; ++i) {
        Rng r = rng.with("founder").with(i);
        int camp = static_cast<int>(r.range(0, kCamps - 1, 0));
        Day age = static_cast<Day>(r.uniform(1) * 40.0 * kDaysPerYear);
        WandererId id = spawn(world, make_name(r.with("name")), world.today() - age, camp, {}, {});

        Event e;
        e.type = kArrival;
        e.importance = Importance::Notable;
        e.participants = {{ref(id), kRoleWanderer}};
        e.fields = {{0, camp_names_[camp]}};
        last_event_[id.index] = world.record(std::move(e));
        book_next(world, id);
    }
}

void WanderersSystem::book_next(World& world, WandererId id) {
    Rng rng = world.rng("wanderers").with(id.index).with(world.today()).with("next");
    Day wait = rng.range(30, 400);
    agenda_.book(world.today() + wait, id.index, id.generation, kActNext);
}

void WanderersSystem::update(World& world) {
    if (cadence_due(Cadence::Yearly, world.today())) hard_winters(world);
    agenda_.run_due(world.today(), [&](const AgendaItem& item) {
        WandererId id{item.subject, item.generation};
        if (ids_.alive(id) && alive(id.index)) act(world, id);
    });
}

void WanderersSystem::act(World& world, WandererId id) {
    const std::uint32_t i = id.index;
    const Day today = world.today();
    const Day age = today - born_[i];

    if (age >= lifespan_days_[i]) {
        die(world, id, Cause::from_condition(kCondAge, static_cast<double>(age) / kDaysPerYear, ref(id)));
        return;
    }

    Rng rng = world.rng("wanderers").with(i).with(today).with("act");
    double roll = rng.uniform(0);
    std::vector<Cause> causes;
    if (last_event_[i] != kNoEvent) causes.push_back(Cause::from_event(last_event_[i]));

    if (roll < 0.5) {
        int to = static_cast<int>(rng.range(0, kCamps - 2, 1));
        if (to >= camp_[i]) ++to;
        Event e;
        e.type = kJourney;
        e.importance = Importance::Trivial;
        e.participants = {{ref(id), kRoleWanderer}};
        e.causes = causes;
        e.fields = {{0, camp_names_[camp_[i]]}, {1, camp_names_[to]}};
        camp_[i] = to;
        EventId eid = world.record(std::move(e));
        if (eid != kNoEvent) last_event_[i] = eid;
    } else if (roll < 0.85) {
        // Meet someone else in the same camp.
        std::vector<std::uint32_t> here;
        for (std::uint32_t j : living_)
            if (j != i && camp_[j] == camp_[i]) here.push_back(j);
        if (!here.empty()) {
            std::uint32_t j = here[static_cast<std::size_t>(rng.range(0, static_cast<std::int64_t>(here.size()) - 1, 2))];
            WandererId other{j, ids_.generations_[j]};
            Event e;
            e.type = kMeeting;
            e.importance = Importance::Trivial;
            e.participants = {{ref(id), kRoleWanderer}, {ref(other), kRoleOther}};
            e.causes = causes;
            e.fields = {{0, camp_names_[camp_[i]]}};
            EventId meeting = world.record(std::move(e));
            if (meeting != kNoEvent) {
                last_event_[i] = meeting;
                last_event_[j] = meeting;
            }

            auto years = [&](std::uint32_t k) { return static_cast<double>(today - born_[k]) / kDaysPerYear; };
            bool fertile = years(i) >= 16 && years(i) <= 45 && years(j) >= 16 && years(j) <= 45;
            if (fertile && alive_count() < kPopulationCap && rng.chance(0.3, 3)) {
                WandererId child = spawn(world, make_name(rng.with("child")), today, camp_[i], id, other);
                Event b;
                b.type = kBirth;
                b.importance = Importance::Notable;
                b.participants = {{ref(child), kRoleChild}, {ref(id), kRoleParent}, {ref(other), kRoleParent}};
                if (meeting != kNoEvent) b.causes = {Cause::from_event(meeting)};
                b.fields = {{0, camp_names_[camp_[i]]}};
                last_event_[child.index] = world.record(std::move(b));
                book_next(world, child);
            }
        }
    }
    book_next(world, id);
}

void WanderersSystem::die(World& world, WandererId id, Cause cause) {
    const std::uint32_t i = id.index;
    died_[i] = world.today();
    living_.erase(std::find(living_.begin(), living_.end(), i));
    Event e;
    e.type = kDeath;
    e.importance = Importance::Notable;
    e.participants = {{ref(id), kRoleDeceased}};
    e.causes = {cause};
    e.fields = {{0, camp_names_[camp_[i]]},
                {1, static_cast<std::int64_t>(static_cast<double>(world.today() - born_[i]) / kDaysPerYear)}};
    EventId eid = world.record(std::move(e));
    if (eid != kNoEvent) last_event_[i] = eid;
}

void WanderersSystem::hard_winters(World& world) {
    for (int c = 0; c < kCamps; ++c) {
        Rng rng = world.rng("wanderers").with("winter").with(world.today()).with(c);
        if (!rng.chance(0.04, 0)) continue;
        double severity = 0.5 + 0.5 * rng.uniform(1);

        Event e;
        e.type = kHardWinter;
        e.importance = Importance::Major;
        e.causes = {Cause::from_condition(kCondCold, severity)};
        e.fields = {{0, camp_names_[c]}, {1, severity}};
        EventId winter = world.record(std::move(e));

        const auto living = living_;  // die() edits living_
        for (std::uint32_t i : living) {
            if (camp_[i] != c) continue;
            if (rng.with(i).chance(severity * 0.3))
                die(world, WandererId{i, ids_.generations_[i]},
                    winter != kNoEvent ? Cause::from_event(winter) : Cause::from_condition(kCondCold, severity));
        }
    }
}

std::size_t WanderersSystem::alive_count() const { return living_.size(); }

std::string WanderersSystem::name_of(EntityRef r) const {
    if (r.kind != EntityKind::Wanderer || r.index >= names_.size()) return {};
    return names_[r.index];
}

void WanderersSystem::list(EntityKind, bool alive_only, std::vector<EntityRef>& out) const {
    if (alive_only) {
        for (std::uint32_t i : living_) out.push_back(ref(WandererId{i, ids_.generations_[i]}));
        return;
    }
    for (std::uint32_t i = 0; i < ids_.capacity(); ++i) out.push_back(ref(WandererId{i, ids_.generations_[i]}));
}

void WanderersSystem::inspect(EntityRef r, const World& world, std::vector<InspectRow>& rows) const {
    if (r.index >= names_.size()) return;
    const auto i = r.index;
    rows.push_back({"Name", names_[i], {}});
    rows.push_back({"Born", format_date(born_[i]), {}});
    if (alive(i)) {
        rows.push_back({"Status", "alive", {}});
        rows.push_back({"Age", std::to_string(static_cast<int>((world.today() - born_[i]) / kDaysPerYear)), {}});
    } else {
        rows.push_back({"Died", format_date(died_[i]), {}});
        rows.push_back({"Age at death", std::to_string(static_cast<int>((died_[i] - born_[i]) / kDaysPerYear)), {}});
    }
    rows.push_back({"Camp", camp_names_[camp_[i]], {}});
    rows.push_back({"Lifespan (fated)", std::to_string(static_cast<int>(lifespan_days_[i] / kDaysPerYear)) + " years", {}});
    if (mother_[i].valid()) rows.push_back({"Parent", names_[mother_[i].index], ref(mother_[i])});
    if (father_[i].valid()) rows.push_back({"Parent", names_[father_[i].index], ref(father_[i])});
}

void WanderersSystem::snapshot_layers(std::vector<SnapshotLayer>& layers) const {
    SnapshotLayer l{"wanderers per camp", std::vector<float>(kCamps, 0.0f)};
    for (std::uint32_t i : living_) l.values[camp_[i]] += 1.0f;
    layers.push_back(std::move(l));
}

void WanderersSystem::save(BinaryWriter& w) const {
    w.put_vector(ids_.generations_);
    w.put_vector(ids_.alive_);
    w.put_vector(ids_.free_);
    w.put(ids_.alive_count_);
    w.put<std::uint64_t>(names_.size());
    for (const auto& n : names_) w.put_string(n);
    w.put_vector(born_);
    w.put_vector(died_);
    w.put_vector(lifespan_days_);
    w.put_vector(camp_);
    for (auto id : mother_) put_id(w, id);
    for (auto id : father_) put_id(w, id);
    w.put_vector(last_event_);
    w.put<std::uint32_t>(static_cast<std::uint32_t>(camp_names_.size()));
    for (const auto& n : camp_names_) w.put_string(n);
    agenda_.save(w);
}

void WanderersSystem::load(BinaryReader& r) {
    ids_.generations_ = r.get_vector<std::uint32_t>();
    ids_.alive_ = r.get_vector<std::uint8_t>();
    ids_.free_ = r.get_vector<std::uint32_t>();
    ids_.alive_count_ = r.get<std::uint32_t>();
    auto n = r.get<std::uint64_t>();
    if (n != ids_.generations_.size()) throw FormatError("wanderer table size mismatch");
    names_.resize(static_cast<std::size_t>(n));
    for (auto& s : names_) s = r.get_string();
    born_ = r.get_vector<Day>();
    died_ = r.get_vector<Day>();
    lifespan_days_ = r.get_vector<Day>();
    camp_ = r.get_vector<std::int32_t>();
    mother_.resize(static_cast<std::size_t>(n));
    father_.resize(static_cast<std::size_t>(n));
    for (auto& id : mother_) id = get_id(r);
    for (auto& id : father_) id = get_id(r);
    last_event_ = r.get_vector<EventId>();
    camp_names_.resize(r.get<std::uint32_t>());
    for (auto& s : camp_names_) s = r.get_string();
    agenda_.load(r);
    living_.clear();
    for (std::uint32_t i = 0; i < died_.size(); ++i)
        if (alive(i)) living_.push_back(i);
    if (born_.size() != n || died_.size() != n || lifespan_days_.size() != n || camp_.size() != n ||
        last_event_.size() != n)
        throw FormatError("wanderer column size mismatch");
}

std::vector<std::unique_ptr<System>> make_standard_systems() {
    std::vector<std::unique_ptr<System>> systems;
    systems.push_back(std::make_unique<WanderersSystem>());
    return systems;
}

}  // namespace hsim
