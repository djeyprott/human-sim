#include "hsim/catalog.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace hsim {

void EventCatalog::add_type(EventTypeInfo info) {
    if (types_.count(info.code)) throw std::logic_error("duplicate event type code: " + info.name);
    types_[info.code] = std::move(info);
}

void EventCatalog::add_role(Role code, std::string name) {
    auto it = roles_.find(code);
    if (it != roles_.end() && it->second != name) throw std::logic_error("conflicting role code: " + name);
    roles_[code] = std::move(name);
}

void EventCatalog::add_condition(ConditionInfo info) {
    if (conditions_.count(info.code)) throw std::logic_error("duplicate condition code: " + info.name);
    conditions_[info.code] = std::move(info);
}

const EventTypeInfo* EventCatalog::type(EventType code) const {
    auto it = types_.find(code);
    return it == types_.end() ? nullptr : &it->second;
}

const std::string& EventCatalog::role_name(Role code) const {
    static const std::string unknown = "?";
    auto it = roles_.find(code);
    return it == roles_.end() ? unknown : it->second;
}

const ConditionInfo* EventCatalog::condition(std::uint16_t code) const {
    auto it = conditions_.find(code);
    return it == conditions_.end() ? nullptr : &it->second;
}

std::vector<const EventTypeInfo*> EventCatalog::all_types() const {
    std::vector<const EventTypeInfo*> out;
    for (auto& [code, info] : types_) out.push_back(&info);
    std::sort(out.begin(), out.end(), [](auto* a, auto* b) { return a->code < b->code; });
    return out;
}

int EventCatalog::field_key(EventType type, const std::string& name) const {
    const auto* info = this->type(type);
    if (!info) return -1;
    for (std::size_t i = 0; i < info->fields.size(); ++i)
        if (info->fields[i] == name) return static_cast<int>(i);
    return -1;
}

std::string EventCatalog::format_value(const FieldValue& v, const NameFn& name_of) const {
    struct Visitor {
        const NameFn& name_of;
        std::string operator()(std::int64_t x) const { return std::to_string(x); }
        std::string operator()(double x) const {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.4g", x);
            return buf;
        }
        std::string operator()(const std::string& s) const { return s; }
        std::string operator()(EntityRef r) const { return name_of(r); }
    };
    return std::visit(Visitor{name_of}, v);
}

namespace {
// Replaces each {name} in `tmpl` with lookup(name). Unknown names stay as-is.
std::string fill(const std::string& tmpl, const std::function<std::optional<std::string>(const std::string&)>& lookup) {
    std::string out;
    out.reserve(tmpl.size() + 32);
    for (std::size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == '{') {
            auto close = tmpl.find('}', i);
            if (close != std::string::npos) {
                auto key = tmpl.substr(i + 1, close - i - 1);
                if (auto v = lookup(key)) {
                    out += *v;
                    i = close + 1;
                    continue;
                }
            }
        }
        out += tmpl[i++];
    }
    return out;
}
}  // namespace

std::string EventCatalog::summarize(const Event& e, const NameFn& name_of) const {
    const auto* info = type(e.type);
    if (!info) return "Unknown event #" + std::to_string(e.type);
    return fill(info->summary, [&](const std::string& key) -> std::optional<std::string> {
        // Participants with this role (joined with "and").
        std::vector<std::string> names;
        for (const auto& p : e.participants)
            if (role_name(p.role) == key) names.push_back(name_of(p.entity));
        if (!names.empty()) {
            std::string s;
            for (std::size_t i = 0; i < names.size(); ++i) {
                if (i) s += (i + 1 == names.size()) ? " and " : ", ";
                s += names[i];
            }
            return s;
        }
        for (const auto& f : e.fields)
            if (f.key < info->fields.size() && info->fields[f.key] == key) return format_value(f.value, name_of);
        return std::nullopt;
    });
}

std::string EventCatalog::describe_cause(const Cause& c, const NameFn& name_of,
                                         const std::function<std::string(EventId)>& event_summary) const {
    switch (c.kind) {
        case Cause::Kind::Event:
            return event_summary(c.event);
        case Cause::Kind::Intervention:
            return "divine intervention";
        case Cause::Kind::Condition: {
            const auto* info = condition(c.condition);
            if (!info) return "unknown condition";
            std::string text = fill(info->format, [&](const std::string& key) -> std::optional<std::string> {
                char buf[64];
                if (key == "value") {
                    std::snprintf(buf, sizeof buf, "%.4g", c.value);
                    return std::string(buf);
                }
                if (key == "pct") {
                    std::snprintf(buf, sizeof buf, "%.0f%%", c.value * 100.0);
                    return std::string(buf);
                }
                if (key == "int") return std::to_string(static_cast<long long>(std::floor(c.value)));
                if (key == "subject") return c.subject.valid() ? name_of(c.subject) : std::string("-");
                return std::nullopt;
            });
            return text;
        }
    }
    return {};
}

}  // namespace hsim
