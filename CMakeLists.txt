cmake_minimum_required(VERSION 3.20)
project(Agario3D VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
set(HUYNITY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../Huynity" CACHE PATH "Checkout vlapugb/Huyunity master")
set(HUYNITY_BUILD_ADAPTERS ON)
set(HUYNITY_BUILD_EXAMPLES OFF)
add_subdirectory("${HUYNITY_SOURCE_DIR}" huynity)
find_package(SDL3 REQUIRED CONFIG)
add_library(agar_match
    game/core/camera.cpp
    game/core/deformation.cpp
    game/core/match.cpp
    game/core/shell.cpp
    game/core/spike.cpp
)
target_include_directories(agar_match PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/game")
target_link_libraries(agar_match PUBLIC huynity::objectlib)

add_library(agar_session game/session/local_game.cpp game/session/menu.cpp)
target_link_libraries(agar_session PUBLIC agar_match SDL3::SDL3)

add_library(agar_cells game/render/cell_renderer.cpp game/render/gl.cpp)
target_link_libraries(agar_cells PUBLIC agar_match SDL3::SDL3)

add_executable(agario3d game/main.cpp game/render/view.cpp game/render/hud.cpp)
target_link_libraries(agario3d PRIVATE agar_session agar_cells huynity::engine)
if(WIN32)
    # glcorearb.h получает соглашение о вызовах без макросов near/far из windows.h.
    target_compile_definitions(huynity_renderer PRIVATE APIENTRY=__stdcall)
    # Оконный адаптер Huynity вызывает расширения OpenGL напрямую.
    find_path(EPOXY_INCLUDE_DIR epoxy/gl.h REQUIRED)
    find_library(EPOXY_LIBRARY NAMES epoxy REQUIRED)
    target_include_directories(huynity_window PRIVATE "${EPOXY_INCLUDE_DIR}")
    target_link_libraries(huynity_window PRIVATE "${EPOXY_LIBRARY}")
    if(MSVC)
        target_compile_options(huynity_window PRIVATE /FIepoxy/gl.h)
    else()
        target_compile_options(huynity_window PRIVATE -include epoxy/gl.h)
    endif()
    if(MINGW)
        target_link_options(agario3d PRIVATE -static -mwindows)
    endif()
endif()
add_custom_command(TARGET agario3d POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/hud.bmp" "$<TARGET_FILE_DIR:agario3d>/hud.bmp")
set_property(TARGET agario3d APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/hud.bmp")
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(agar_match PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(agar_session PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(agar_cells PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(agario3d PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

include(packaging/packages.cmake)
