#pragma once
// An agenda: things scheduled to happen on a given day.
//
// Historical figures (and the Phase 0 placeholder) are event-driven: instead
// of being updated every tick, each one books its next action on the agenda
// and is only touched when that day comes. Ties on the same day are broken by
// booking order, which keeps runs deterministic.

#include <cstdint>
#include <queue>
#include <vector>

#include "hsim/binary_io.hpp"
#include "hsim/calendar.hpp"

namespace hsim {

struct AgendaItem {
    Day day = 0;
    std::uint64_t seq = 0;     // booking order, for deterministic ties
    std::uint32_t subject = 0; // usually an entity index
    std::uint32_t generation = 0;
    std::uint16_t action = 0;  // system-defined
};

class Agenda {
public:
    void book(Day day, std::uint32_t subject, std::uint32_t generation, std::uint16_t action) {
        heap_.push(AgendaItem{day, next_seq_++, subject, generation, action});
    }

    // Pops every item due on or before `day`, in order.
    template <class F>
    void run_due(Day day, F&& f) {
        while (!heap_.empty() && heap_.top().day <= day) {
            AgendaItem item = heap_.top();
            heap_.pop();
            f(item);
        }
    }

    std::size_t size() const { return heap_.size(); }

    void save(BinaryWriter& w) const {
        auto copy = heap_;
        w.put(next_seq_);
        w.put<std::uint64_t>(copy.size());
        while (!copy.empty()) {
            const auto& i = copy.top();
            w.put(i.day);
            w.put(i.seq);
            w.put(i.subject);
            w.put(i.generation);
            w.put(i.action);
            copy.pop();
        }
    }

    void load(BinaryReader& r) {
        heap_ = {};
        next_seq_ = r.get<std::uint64_t>();
        auto n = r.get<std::uint64_t>();
        if (n > r.remaining()) throw FormatError("agenda size out of range");
        for (std::uint64_t k = 0; k < n; ++k) {
            AgendaItem i;
            i.day = r.get<Day>();
            i.seq = r.get<std::uint64_t>();
            i.subject = r.get<std::uint32_t>();
            i.generation = r.get<std::uint32_t>();
            i.action = r.get<std::uint16_t>();
            heap_.push(i);
        }
    }

private:
    struct Later {
        bool operator()(const AgendaItem& a, const AgendaItem& b) const {
            return a.day != b.day ? a.day > b.day : a.seq > b.seq;
        }
    };
    std::priority_queue<AgendaItem, std::vector<AgendaItem>, Later> heap_;
    std::uint64_t next_seq_ = 0;
};

}  // namespace hsim
