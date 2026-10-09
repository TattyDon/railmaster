find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(railmaster main.cpp)

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
