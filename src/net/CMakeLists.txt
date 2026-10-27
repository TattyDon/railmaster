# Multiplayer over a network (lockstep): TCP messages and the host's and
# guests' sessions. Depends on the simulation, not on the client.

add_library(railmaster_net
    src/session.cpp
    src/socket.cpp
)
add_library(railmaster::net ALIAS railmaster_net)

target_include_directories(railmaster_net PUBLIC include)
target_link_libraries(railmaster_net
    PUBLIC railmaster::sim
    PRIVATE railmaster_warnings)
if(WIN32)
    target_link_libraries(railmaster_net PRIVATE ws2_32 iphlpapi)
endif()
