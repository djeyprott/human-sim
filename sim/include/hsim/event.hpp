#pragma once
// Events: the recorded history of the world.
//
// Every event says when and where it happened, what kind of event it was,
// who took part (and in what role), and what caused it. The legends viewer
// reads these, and following `causes` backwards is how a user (or a
// developer) finds out *why* something happened.

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "hsim/calendar.hpp"
#include "hsim/ids.hpp"

namespace hsim {

using EventId = std::uint64_t;  // sequential from 1; 0 = none
constexpr EventId kNoEvent = 0;

enum class Importance : std::uint8_t {
    Trivial = 0,
    Notable = 1,
    Major = 2,
    Epochal = 3,
};
const char* importance_name(Importance i);

// Event types are registered at startup by the systems that emit them.
// The numeric code is stored in history files, so it must stay stable:
// each system owns a fixed range (see event_types.hpp).
using EventType = std::uint16_t;

// A named role a participant plays in an event ("deceased", "heir", ...).
using Role = std::uint16_t;

struct Participant {
    EntityRef entity;
    Role role = 0;
    friend bool operator==(const Participant&, const Participant&) = default;
};

// A cause is either an earlier event, or a recorded condition of the world
// ("harvest at 40% of normal", subject = the cell or cohort it applied to).
struct Cause {
    enum class Kind : std::uint8_t { Event = 0, Condition = 1, Intervention = 2 };
    Kind kind = Kind::Event;
    EventId event = kNoEvent;       // Kind::Event
    std::uint16_t condition = 0;    // Kind::Condition: registered condition code
    double value = 0.0;             // Kind::Condition: e.g. 0.4 for 40%
    EntityRef subject;              // Kind::Condition: what it applied to

    static Cause from_event(EventId id) { return Cause{Kind::Event, id, 0, 0.0, {}}; }
    static Cause from_condition(std::uint16_t code, double value, EntityRef subject = {}) {
        return Cause{Kind::Condition, kNoEvent, code, value, subject};
    }
    static Cause intervention() { return Cause{Kind::Intervention, kNoEvent, 0, 0.0, {}}; }
    friend bool operator==(const Cause&, const Cause&) = default;
};

// Type-specific details, as named fields. Field keys are registered per
// event type, so the viewer can show any event without special code.
using FieldValue = std::variant<std::int64_t, double, std::string, EntityRef>;
struct Field {
    std::uint16_t key = 0;
    FieldValue value;
    friend bool operator==(const Field&, const Field&) = default;
};

constexpr std::uint32_t kNoPlace = 0xFFFFFFFFu;

struct Event {
    EventId id = kNoEvent;
    Day day = 0;
    std::uint32_t place = kNoPlace;  // a cell index, when the event has one
    EventType type = 0;
    Importance importance = Importance::Trivial;
    std::vector<Participant> participants;
    std::vector<Cause> causes;
    std::vector<Field> fields;

    friend bool operator==(const Event&, const Event&) = default;
};

}  // namespace hsim
