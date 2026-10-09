find_package(doctest QUIET)
if(NOT doctest_FOUND)
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.4.11)
    FetchContent_MakeAvailable(doctest)
endif()

add_executable(railmaster_tests
    main.cpp
    test_balance.cpp
    test_bankruptcy.cpp
    test_cargo.cpp
    test_commands.cpp
    test_company.cpp
    test_consist.cpp
    test_game_speed.cpp
    test_map_scale.cpp
    test_price_field.cpp
    test_corporate.cpp
    test_date.cpp
    test_economic_conditions.cpp
    test_economy.cpp
    test_electrification.cpp
    test_express.cpp
    test_freight.cpp
    test_industry_ownership.cpp
    test_locomotive.cpp
    test_money.cpp
    test_random.cpp
    test_railway.cpp
    test_sentiment.cpp
    test_rivals.cpp
    test_servicing.cpp
    test_station_buildings.cpp
    test_territories.cpp
    test_stock.cpp
    test_terrain.cpp
    test_track.cpp
    test_towns_ports.cpp
    test_track_builder.cpp
    test_warehouses_spawning.cpp
    test_world.cpp
)
target_link_libraries(railmaster_tests PRIVATE railmaster::sim doctest::doctest railmaster_warnings)
target_compile_definitions(railmaster_tests PRIVATE
    RAILMASTER_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

add_test(NAME railmaster_tests COMMAND railmaster_tests)
