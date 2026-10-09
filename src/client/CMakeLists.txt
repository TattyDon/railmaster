find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_executable(railmaster main.cpp)

target_link_libraries(railmaster PRIVATE
    railmaster::sim
    SDL2::SDL2
    OpenGL::GL
    railmaster_warnings)

if(TARGET SDL2::SDL2main)
    target_link_libraries(railmaster PRIVATE SDL2::SDL2main)
endif()
