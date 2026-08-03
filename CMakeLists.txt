cmake_minimum_required(VERSION 3.20)
project(UniVim VERSION 1.0.12 LANGUAGES C OBJC)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Compiler flags matching makefile
add_compile_definitions(
    HAVE_CONFIG_H
    MACOS_X
    MACOS_X_DARWIN
)

add_compile_options(
    -Wall
    -Wno-array-bounds
    -Wno-unknown-warning-option
    -Wno-cpp
    -Wno-pointer-sign
    -Wno-unused-parameter
    -Wno-strict-overflow
    -Wno-return-type
    -Werror
    -g
    -O2
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/lib
    ${CMAKE_SOURCE_DIR}/lib/libvim/proto
)

# Source files
set(SOURCES
    src/main.m
    src/helpers.m
    src/helpers.c
    src/workspace.m
    src/event_tap.c
    src/ax.c
    src/buffer.c
    src/line.c
    src/env_vars.c
    src/vn_engine.c
    src/vn_input.c
    src/toast.m
    src/config_watcher.c
    src/codesign_selfheal.c
)

# Main executable
add_executable(univim ${SOURCES})

# Link libraries
target_link_libraries(univim
    ${CMAKE_SOURCE_DIR}/lib/libvim.a
    ${CMAKE_SOURCE_DIR}/lib/libunikey.a
    m
    ncurses
    iconv
    c++
    "-framework Carbon"
    "-framework Cocoa"
)

# Generate Info.plist from template
set(APP_BUNDLE_DIR ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/UniVim.app)
configure_file(
    ${CMAKE_SOURCE_DIR}/cmake/Info.plist.in
    ${CMAKE_BINARY_DIR}/Info.plist
    @ONLY
)

# Custom target: app bundle
add_custom_target(app
    DEPENDS univim
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${APP_BUNDLE_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_BUNDLE_DIR}/Contents/MacOS
    COMMAND ${CMAKE_COMMAND} -E make_directory ${APP_BUNDLE_DIR}/Contents/Resources
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:univim> ${APP_BUNDLE_DIR}/Contents/MacOS/univim
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/icons/uv.icns ${APP_BUNDLE_DIR}/Contents/Resources/uv.icns
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/Info.plist ${APP_BUNDLE_DIR}/Contents/Info.plist
    COMMENT "Creating UniVim.app bundle"
)

# Custom target: sign-app
add_custom_target(sign-app
    DEPENDS app
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/ensure_codesign_cert.sh univim-cert
    COMMAND codesign --force --sign univim-cert ${APP_BUNDLE_DIR}
    COMMENT "Signing UniVim.app"
)

# Custom target: bundle (distribution tarball)
add_custom_target(bundle
    DEPENDS sign-app
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bundle
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:univim> ${CMAKE_BINARY_DIR}/bundle/
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/examples ${CMAKE_BINARY_DIR}/bundle/examples
    COMMAND ${CMAKE_COMMAND} -E tar czf ${CMAKE_BINARY_DIR}/bundle_${PROJECT_VERSION}.tgz ${CMAKE_BINARY_DIR}/bundle
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${CMAKE_BINARY_DIR}/bundle
    COMMENT "Creating distribution bundle"
)

# Custom target: build libvim from submodule
add_custom_target(lib
    COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_SOURCE_DIR}/libvim/src make
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/libvim/src/libvim.a ${CMAKE_SOURCE_DIR}/lib/libvim.a
    COMMENT "Building libvim from submodule"
)
