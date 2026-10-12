find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(railmaster
    camera.cpp
    font.cpp
    main.cpp
    render.cpp
    shading.cpp
    tools.cpp
)

# Which commit this is, shown in the corner of the screen: refreshed on
# every build, so it is never stale.
set(RAILMASTER_BUILD_INFO ${CMAKE_CURRENT_BINARY_DIR}/generated/build_info.hpp)
add_custom_target(railmaster_build_info
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${RAILMASTER_BUILD_INFO}
            -P ${PROJECT_SOURCE_DIR}/cmake/build_info.cmake
    BYPRODUCTS ${RAILMASTER_BUILD_INFO}
    COMMENT "Recording the build's commit")
add_dependencies(railmaster railmaster_build_info)
target_include_directories(railmaster PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(railmaster PRIVATE
    railmaster::sim
    SDL2::SDL2
    OpenGL::GL
    railmaster_warnings)

# Lets the client run straight from the build tree; pass a path to override.
target_compile_definitions(railmaster PRIVATE
    RAILMASTER_DEFAULT_DATA_DIR="${PROJECT_SOURCE_DIR}/data")

if(TARGET SDL2::SDL2main)
    target_link_libraries(railmaster PRIVATE SDL2::SDL2main)
endif()
