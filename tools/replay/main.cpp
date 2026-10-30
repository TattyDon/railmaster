// railmaster_replay: play a saved game again from its start, using the
// command log kept in the save, and check it ends exactly as saved (the
// simulation is deterministic, so it should). For debugging and for
// checking a change keeps old games playing the same (rt3-clone-spec §16).
//
//   railmaster_replay GAME.sav [DATA_DIR]
//
// Exits 0 when the replay matches the save byte for byte, 1 when it does
// not, 2 when the save cannot be read or replayed.
//
//   railmaster_replay --make GAME.sav [DAYS] [DATA_DIR]
//
// Makes a game to check with: a small map with rivals and territories, the
// demo railway, DAYS of play (120 if not given), saved. Builds of the game
// from different compilers must make it byte for byte alike, and replay
// each other's (CI checks both).

#include "railmaster/sim/demo.hpp"
#include "railmaster/sim/world.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace railmaster::sim;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

GameData load_data(const std::string& dir) {
    GameData d;
    d.balance = Balance::from_json(read_file(dir + "/balance.json"));
    d.cargo = CargoRegistry::from_json(read_file(dir + "/cargo.json"), d.balance.economy.cargo_price_unit);
    d.locomotives = LocomotiveRegistry::from_json(read_file(dir + "/locomotives.json"));
    d.industries = IndustryRegistry::from_json(read_file(dir + "/industries.json"), d.cargo);
    d.tycoons = TycoonRegistry::from_json(read_file(dir + "/tycoons.json"));
    return d;
}

int make(const std::string& out, int days, const std::string& dir) {
    WorldConfig cfg;
    cfg.width_tiles = cfg.height_tiles = 128;
    cfg.seed = 2024;
    cfg.rivals = 2;
    cfg.territories = 4;
    World w(cfg, load_data(dir));
    (void)build_demo_network(w); // logged commands, as a player's
    for (int t = 0; t < days * World::kTicksPerDay; ++t) w.tick();
    std::ofstream file(out, std::ios::binary);
    const std::string bytes = w.save();
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!file) throw std::runtime_error("cannot write " + out);
    std::printf("made %s: %d days, %zu commands, checksum %016llx\n", out.c_str(), days, w.command_log().size(),
                static_cast<unsigned long long>(w.checksum()));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 3 && std::string(argv[1]) == "--make") {
        try {
            const int days = argc > 3 ? std::stoi(argv[3]) : 120;
            return make(argv[2], days, argc > 4 ? argv[4] : RAILMASTER_DEFAULT_DATA_DIR);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "%s\n", e.what());
            return 2;
        }
    }
    if (argc < 2) {
        std::fprintf(stderr, "usage: railmaster_replay GAME.sav [DATA_DIR], or railmaster_replay --make GAME.sav [DAYS] [DATA_DIR]\n");
        return 2;
    }
    const std::string dir = argc > 2 ? argv[2] : RAILMASTER_DEFAULT_DATA_DIR;
    try {
        const std::string saved = read_file(argv[1]);
        const GameData data = load_data(dir);
        const World original = World::load(saved, data);
        std::printf("%s: %d-%02d-%02d, %zu commands logged, %llu ticks\n", argv[1], original.date().year(),
                    original.date().month(), original.date().calendar().day, original.command_log().size(),
                    static_cast<unsigned long long>(original.total_ticks()));
        const auto start = std::chrono::steady_clock::now();
        const World again = World::replay(saved, data);
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (again.save() == saved) {
            std::printf("replay matches the save (%lld ms)\n", static_cast<long long>(ms));
            return 0;
        }
        std::printf("replay DIFFERS from the save (%lld ms)\n", static_cast<long long>(ms));
        return 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
