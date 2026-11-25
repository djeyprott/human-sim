// humansim: headless command-line runner.
//
//   humansim generate --seed 42 --until 1500CE --out world.hsim [--from 10000BCE]
//                     [--min-importance trivial|notable|major|epochal] [--snapshot-years 10]
//   humansim continue world.hsim --until 2000BCE [--out other.hsim]
//   humansim info world.hsim
//   humansim events world.hsim [--from Y] [--to Y] [--min notable] [--limit 50] [--causes]
//   humansim entity world.hsim <kind> <index>
//
// Years accept "10000BCE", "1500CE", "1500", or astronomical "-9999".

#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "hsim/world.hpp"

using namespace hsim;

namespace {

struct Args {
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    bool flag(const std::string& name) const { return options.count(name) > 0; }
    std::optional<std::string> get(const std::string& name) const {
        auto it = options.find(name);
        if (it == options.end()) return std::nullopt;
        return it->second;
    }
};

Args parse_args(int argc, char** argv, int start) {
    Args a;
    for (int i = start; i < argc; ++i) {
        std::string s = argv[i];
        if (s.rfind("--", 0) == 0) {
            std::string key = s.substr(2);
            if (i + 1 < argc && std::string(argv[i + 1]).rfind("--", 0) != 0)
                a.options[key] = argv[++i];
            else
                a.options[key] = "";
        } else {
            a.positional.push_back(s);
        }
    }
    return a;
}

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "humansim: " << message << "\n";
    std::exit(1);
}

std::int64_t year_arg(const Args& a, const std::string& name, std::int64_t fallback) {
    auto v = a.get(name);
    if (!v) return fallback;
    auto y = parse_year(*v);
    if (!y) fail("could not read year '" + *v + "' (try 10000BCE or 1500CE)");
    return *y;
}

Importance importance_arg(const Args& a, const std::string& name, Importance fallback) {
    auto v = a.get(name);
    if (!v) return fallback;
    if (*v == "trivial") return Importance::Trivial;
    if (*v == "notable") return Importance::Notable;
    if (*v == "major") return Importance::Major;
    if (*v == "epochal") return Importance::Epochal;
    fail("importance must be trivial, notable, major or epochal");
}

void print_usage() {
    std::cout <<
        "usage:\n"
        "  humansim generate --seed N --until YEAR --out FILE [--from YEAR]\n"
        "                    [--min-importance LEVEL] [--snapshot-years N] [--threads N]\n"
        "  humansim continue FILE --until YEAR [--out FILE]\n"
        "  humansim info FILE\n"
        "  humansim events FILE [--from YEAR] [--to YEAR] [--min LEVEL] [--limit N] [--causes]\n"
        "  humansim entity FILE KIND INDEX\n";
}

void run_with_progress(World& world, Day until) {
    auto start = std::chrono::steady_clock::now();
    auto last_print = start;
    world.run_until(until, [&] {
        auto now = std::chrono::steady_clock::now();
        if (now - last_print > std::chrono::seconds(1)) {
            last_print = now;
            std::cerr << "\r  " << format_year(to_date(world.today()).year) << "  "
                      << world.events().total_events() << " events      " << std::flush;
        }
        return false;
    });
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cerr << "\r  reached " << format_date(world.today()) << " in " << secs << " s, "
              << world.events().total_events() << " events recorded        \n";
}

int cmd_generate(const Args& a) {
    auto out = a.get("out");
    if (!out) fail("generate needs --out FILE");
    if (!a.get("until")) fail("generate needs --until YEAR");

    SessionDir session;
    WorldSettings s;
    s.seed = std::strtoull(a.get("seed").value_or("1").c_str(), nullptr, 10);
    s.start_year = year_arg(a, "from", -9999);
    s.history_dir = session.path();
    s.min_importance = importance_arg(a, "min-importance", Importance::Trivial);
    s.snapshot_interval_years = std::atoi(a.get("snapshot-years").value_or("10").c_str());
    s.threads = static_cast<unsigned>(std::atoi(a.get("threads").value_or("0").c_str()));

    World world(s);
    world.init_new();
    std::cerr << "Generating seed " << s.seed << " from " << format_year(s.start_year) << "\n";
    run_with_progress(world, first_day_of_year(year_arg(a, "until", 0)));
    world.save(*out);
    std::cout << "Saved " << *out << " (state hash " << std::hex << world.state_hash() << std::dec << ")\n";
    return 0;
}

void load_into(World& world, const std::string& path) {
    try {
        world.load(path);
    } catch (const std::exception& e) {
        fail("cannot load " + path + ": " + e.what());
    }
}

WorldSettings session_settings(const SessionDir& session) {
    WorldSettings s;
    s.history_dir = session.path();
    return s;
}

int cmd_continue(const Args& a) {
    if (a.positional.empty()) fail("continue needs a save file");
    SessionDir session;
    World world(session_settings(session));
    load_into(world, a.positional[0]);
    run_with_progress(world, first_day_of_year(year_arg(a, "until", to_date(world.today()).year + 100)));
    auto out = a.get("out").value_or(a.positional[0]);
    world.save(out);
    std::cout << "Saved " << out << " (state hash " << std::hex << world.state_hash() << std::dec << ")\n";
    return 0;
}

int cmd_info(const Args& a) {
    if (a.positional.empty()) fail("info needs a save file");
    SessionDir session;
    World world(session_settings(session));
    load_into(world, a.positional[0]);
    std::cout << "Seed:        " << world.seed() << "\n"
              << "Started:     " << format_year(world.settings().start_year) << "\n"
              << "Today:       " << format_date(world.today()) << "\n"
              << "Events:      " << world.events().total_events() << " (recording "
              << importance_name(world.events().min_importance()) << " and above)\n"
              << "Snapshots:   " << world.snapshots().days().size() << " (every "
              << world.snapshots().interval_years() << " years)\n"
              << "State hash:  " << std::hex << world.state_hash() << std::dec << "\n";
    for (const auto& sys : world.systems()) {
        for (auto kind : {EntityKind::Wanderer, EntityKind::Cell, EntityKind::Cohort, EntityKind::Person, EntityKind::Org}) {
            if (!sys->owns(kind)) continue;
            auto all = world.list(kind, false).size();
            auto living = world.list(kind, true).size();
            std::cout << entity_kind_name(kind) << "s:   " << living << " alive, " << all << " ever\n";
        }
    }
    return 0;
}

int cmd_events(const Args& a) {
    if (a.positional.empty()) fail("events needs a save file");
    SessionDir session;
    World world(session_settings(session));
    load_into(world, a.positional[0]);
    Day from = a.get("from") ? first_day_of_year(year_arg(a, "from", 0)) : INT64_MIN;
    Day to = a.get("to") ? first_day_of_year(year_arg(a, "to", 0) + 1) - 1 : INT64_MAX;
    Importance min = importance_arg(a, "min", Importance::Trivial);
    long limit = std::atol(a.get("limit").value_or("50").c_str());
    bool causes = a.flag("causes");

    long shown = 0;
    world.events().scan(from, to, [&](const Event& e) {
        if (e.importance < min) return true;
        std::cout << "#" << e.id << "  " << format_date(e.day) << "  [" << importance_name(e.importance) << "]  "
                  << world.summarize(e) << "\n";
        if (causes)
            for (const auto& c : e.causes) std::cout << "        because: " << world.describe_cause(c) << "\n";
        return ++shown < limit;
    });
    return 0;
}

int cmd_entity(const Args& a) {
    if (a.positional.size() < 3) fail("entity needs FILE KIND INDEX");
    SessionDir session;
    World world(session_settings(session));
    load_into(world, a.positional[0]);
    std::string kind_name = a.positional[1];
    EntityKind kind = EntityKind::None;
    for (auto k : {EntityKind::Wanderer, EntityKind::Cell, EntityKind::Cohort, EntityKind::Person, EntityKind::Org}) {
        std::string n = entity_kind_name(k);
        for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (n == kind_name) kind = k;
    }
    if (kind == EntityKind::None) fail("unknown entity kind '" + kind_name + "'");
    auto index = static_cast<std::uint32_t>(std::strtoul(a.positional[2].c_str(), nullptr, 10));
    EntityRef ref;
    for (auto r : world.list(kind, false))
        if (r.index == index) ref = r;
    if (!ref.valid()) fail("no such entity");

    for (const auto& row : world.inspect(ref)) std::cout << row.label << ": " << row.value << "\n";
    std::cout << "\nHistory:\n";
    for (EventId id : world.events().events_for(ref)) {
        auto e = world.events().get(id);
        if (e) std::cout << "  " << format_date(e->day) << "  " << world.summarize(*e) << "\n";
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    std::string cmd = argv[1];
    Args args = parse_args(argc, argv, 2);
    try {
        if (cmd == "generate") return cmd_generate(args);
        if (cmd == "continue") return cmd_continue(args);
        if (cmd == "info") return cmd_info(args);
        if (cmd == "events") return cmd_events(args);
        if (cmd == "entity") return cmd_entity(args);
    } catch (const std::exception& e) {
        fail(e.what());
    }
    print_usage();
    return 1;
}
