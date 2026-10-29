find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
find_package(ZLIB REQUIRED) # PNG maps from the map editor

add_executable(railmaster
    audio.cpp
    camera.cpp
    editor.cpp
    font.cpp
    main.cpp
    model.cpp
    png.cpp
    render.cpp
    shading.cpp
    third_party_impl.cpp
    title.cpp
    tools.cpp
    tutorial.cpp
    goals.cpp
    ui.cpp
)

# A trackpad's pinch: read from the system on macOS (SDL2 does not pass it on).
if(APPLE)
    enable_language(OBJCXX)
    target_sources(railmaster PRIVATE gestures_mac.mm)
    target_link_libraries(railmaster PRIVATE "-framework Cocoa")
    # Apple marks OpenGL deprecated; the game uses it on purpose (the
    # renderer is OpenGL 2), so its warnings are not wanted.
    target_compile_definitions(railmaster PRIVATE GL_SILENCE_DEPRECATION)
else()
    target_sources(railmaster PRIVATE gestures.cpp)
endif()

# Single-header libraries for loading art: cgltf (glTF, MIT) and stb_image
# (PNG/JPEG, public domain). As system headers, the project's warnings do
# not apply to them; their implementations are compiled quietly.
target_include_directories(railmaster SYSTEM PRIVATE
    ${PROJECT_SOURCE_DIR}/third_party/cgltf
    ${PROJECT_SOURCE_DIR}/third_party/stb)
set_source_files_properties(third_party_impl.cpp PROPERTIES COMPILE_OPTIONS
    "$<IF:$<CXX_COMPILER_ID:MSVC>,/w,-w>")

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

find_package(Threads REQUIRED)
target_link_libraries(railmaster PRIVATE
    railmaster::sim
    railmaster::net
    Threads::Threads
    SDL2::SDL2
    OpenGL::GL
    ZLIB::ZLIB
    railmaster_warnings)

# Lets the client run straight from the build tree; pass a path to override.
# Installed (cmake --install), it finds its data where the install put it,
# relative to its own folder.
file(RELATIVE_PATH RAILMASTER_INSTALLED_DATA ${CMAKE_INSTALL_FULL_BINDIR} ${CMAKE_INSTALL_FULL_DATADIR}/railmaster)
target_compile_definitions(railmaster PRIVATE
    RAILMASTER_DEFAULT_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
    RAILMASTER_INSTALLED_DATA="${RAILMASTER_INSTALLED_DATA}")

# cmake --install: the game, its data (the art's licences are in it), and on
# Linux a desktop entry and icon, so it starts from the applications menu.
install(TARGETS railmaster RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/data/ DESTINATION ${CMAKE_INSTALL_DATADIR}/railmaster)
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/README.md DESTINATION ${CMAKE_INSTALL_DOCDIR})
if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/railmaster.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/data/icon.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
            RENAME railmaster.png)
endif()

if(TARGET SDL2::SDL2main)
    target_link_libraries(railmaster PRIVATE SDL2::SDL2main)
endif()
