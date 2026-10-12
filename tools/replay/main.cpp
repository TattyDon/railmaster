// railmaster_replay: play a saved game again from its start, using the
// command log kept in the save, and check it ends exactly as saved (the
// simulation is deterministic, so it should). For debugging and for
// checking a change keeps old games playing the same (rt3-clone-spec §16).
//
//   railmaster_replay GAME.sav [DATA_DIR]
//
// Exits 0 when the replay matches the save byte for byte, 1 when it does
// not, 2 when the save cannot be read or replayed.

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

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: railmaster_replay GAME.sav [DATA_DIR]\n");
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
