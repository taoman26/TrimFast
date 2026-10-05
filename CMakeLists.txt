cmake_minimum_required(VERSION 3.21)
project(TrimFast VERSION 0.1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

option(TRIMFAST_BUILD_TESTS "Build unit tests" ON)
option(TRIMFAST_WARNINGS "Compile with extra warnings" ON)
option(TRIMFAST_WERROR "Treat warnings as errors" OFF)
option(TRIMFAST_SANITIZE "Build with AddressSanitizer + UBSan (Linux, GCC/Clang)" OFF)

if(TRIMFAST_WARNINGS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    if(TRIMFAST_WERROR)
        add_compile_options(-Werror)
    endif()
endif()
if(TRIMFAST_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# Qt >= 6.5: Qt Multimedia gained its FFmpeg backend there (older ones use GStreamer).
# A Qt outside the system directories (e.g. one installed with aqtinstall) must still be found by
# the installed program: keep its location in the RPATH. (Set before the targets are created.)
if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Multimedia)

# core: no dependency on Qt Widgets.
add_library(trimfast_core STATIC
    src/core/ExportCoordinator.cpp
    src/core/ExportCoordinator.h
    src/core/FFmpegRunner.cpp
    src/core/FFmpegRunner.h
    src/core/Insta360Trailer.cpp
    src/core/Insta360Trailer.h
    src/core/InsvPairResolver.cpp
    src/core/InsvPairResolver.h
    src/core/KeyframeIndex.cpp
    src/core/KeyframeIndex.h
    src/core/KeyframeLoader.cpp
    src/core/KeyframeLoader.h
    src/core/MarkerManager.cpp
    src/core/MarkerManager.h
    src/core/MediaInfo.h
    src/core/MediaProbe.cpp
    src/core/MediaProbe.h
    src/core/Mp4Atoms.cpp
    src/core/Mp4Atoms.h
    src/core/OutputCheck.cpp
    src/core/OutputCheck.h
    src/core/OutputPath.cpp
    src/core/OutputPath.h
    src/core/OutputVerifier.cpp
    src/core/OutputVerifier.h
    src/core/SessionManager.cpp
    src/core/SessionManager.h
    src/core/SettingsManager.cpp
    src/core/SettingsManager.h
    src/core/TimeFormat.cpp
    src/core/TimeFormat.h
    src/core/TimelineScale.cpp
    src/core/TimelineScale.h
    src/core/ToolLocator.cpp
    src/core/ToolLocator.h
)
target_include_directories(trimfast_core PUBLIC src)
target_link_libraries(trimfast_core PUBLIC Qt6::Core)

# playback: QMediaPlayer wrapper (no Widgets).
add_library(trimfast_playback STATIC
    src/playback/VideoPlayer.cpp
    src/playback/VideoPlayer.h
)
target_link_libraries(trimfast_playback PUBLIC trimfast_core Qt6::Multimedia)

# app: the window and its widgets (shared by the executable and the tests).
add_library(trimfast_app STATIC
    src/app/MainWindow.cpp
    src/app/MainWindow.h
    src/app/TrimFastApplication.cpp
    src/app/TrimFastApplication.h
    src/ui/TimelineWidget.cpp
    src/ui/TimelineWidget.h
    src/ui/VideoSurface.cpp
    src/ui/VideoSurface.h
)
target_link_libraries(trimfast_app PUBLIC trimfast_core trimfast_playback Qt6::Widgets Qt6::Multimedia)

add_executable(trimfast src/main.cpp)
target_link_libraries(trimfast PRIVATE trimfast_app)
# The project's home (also written, as plain text, in the README, AppStream data and package info).
set(TRIMFAST_HOMEPAGE "https://github.com/taoman26/TrimFast")
target_compile_definitions(trimfast PRIVATE TRIMFAST_VERSION="${PROJECT_VERSION}"
                                            TRIMFAST_HOMEPAGE="${TRIMFAST_HOMEPAGE}")
target_compile_definitions(trimfast_app PUBLIC TRIMFAST_HOMEPAGE="${TRIMFAST_HOMEPAGE}")

# --- install (Linux and other Unix; Haiku packages are staged by packaging/haiku) ---
include(GNUInstallDirs)
if(UNIX AND NOT APPLE AND NOT CMAKE_SYSTEM_NAME STREQUAL "Haiku")
    install(TARGETS trimfast RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(FILES packaging/linux/io.github.taoman26.TrimFast.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES packaging/linux/io.github.taoman26.TrimFast.metainfo.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES packaging/linux/icons/trimfast.svg DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
    install(DIRECTORY packaging/linux/icons/hicolor/ DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor
            FILES_MATCHING PATTERN "*.png")
    install(FILES README.md CHANGELOG.md LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/trimfast)
endif()

if(TRIMFAST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
