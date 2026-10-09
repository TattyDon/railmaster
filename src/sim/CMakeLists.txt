# The simulation core: deterministic, headless, no rendering or platform deps.
# Everything that affects game outcomes lives here so it can be unit-tested
# and, later, run in lockstep for multiplayer.

find_package(nlohmann_json 3.10 REQUIRED)

add_library(railmaster_sim
    src/cargo.cpp
    src/date.cpp
    src/locomotive.cpp
    src/railway.cpp
    src/terrain.cpp
    src/track.cpp
    src/track_builder.cpp
    src/world.cpp
)
add_library(railmaster::sim ALIAS railmaster_sim)

target_include_directories(railmaster_sim PUBLIC include)
target_link_libraries(railmaster_sim
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE railmaster_warnings)
