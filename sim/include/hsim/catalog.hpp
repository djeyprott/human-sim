#pragma once
// The event catalog: names and descriptions for event types, roles, fields
// and conditions, registered by the systems that use them.
//
// A summary template refers to participants by role name and to details by
// field name, e.g. "{traveller} walked from {from} to {to}". The viewer and
// the CLI render any event from its template, with no per-type UI code.

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "hsim/event.hpp"

namespace hsim {

struct EventTypeInfo {
    EventType code = 0;
    std::string name;              // short name, e.g. "Journey"
    std::string summary;           // template, e.g. "{traveller} walked from {from} to {to}"
    std::vector<std::string> fields;  // field key -> name (index = key)
};

struct ConditionInfo {
    std::uint16_t code = 0;
    std::string name;    // e.g. "harvest"
    std::string format;  // e.g. "harvest at {pct} of normal", "{value} years old"
};

class EventCatalog {
public:
    // Registration. Codes must be unique and stable (they are saved).
    void add_type(EventTypeInfo info);
    void add_role(Role code, std::string name);
    void add_condition(ConditionInfo info);

    const EventTypeInfo* type(EventType code) const;
    const std::string& role_name(Role code) const;
    const ConditionInfo* condition(std::uint16_t code) const;

    std::vector<const EventTypeInfo*> all_types() const;

    // Field key for a named field of a type (or -1).
    int field_key(EventType type, const std::string& name) const;

    // Name lookup for entities, supplied by the world.
    using NameFn = std::function<std::string(EntityRef)>;

    std::string summarize(const Event& e, const NameFn& name_of) const;
    std::string describe_cause(const Cause& c, const NameFn& name_of,
                               const std::function<std::string(EventId)>& event_summary) const;
    std::string format_value(const FieldValue& v, const NameFn& name_of) const;

private:
    std::unordered_map<EventType, EventTypeInfo> types_;
    std::unordered_map<Role, std::string> roles_;
    std::unordered_map<std::uint16_t, ConditionInfo> conditions_;
};

}  // namespace hsim
