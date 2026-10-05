find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

add_executable(tst_timeformat tst_timeformat.cpp)
target_link_libraries(tst_timeformat PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_timeformat COMMAND tst_timeformat)

add_executable(tst_mediaprobe tst_mediaprobe.cpp)
target_link_libraries(tst_mediaprobe PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_mediaprobe COMMAND tst_mediaprobe)

add_executable(tst_keyframeindex tst_keyframeindex.cpp)
target_link_libraries(tst_keyframeindex PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_keyframeindex COMMAND tst_keyframeindex)

add_executable(tst_timelinescale tst_timelinescale.cpp)
target_link_libraries(tst_timelinescale PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_timelinescale COMMAND tst_timelinescale)

add_executable(tst_markermanager tst_markermanager.cpp)
target_link_libraries(tst_markermanager PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_markermanager COMMAND tst_markermanager)

add_executable(tst_sessionmanager tst_sessionmanager.cpp)
target_link_libraries(tst_sessionmanager PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_sessionmanager COMMAND tst_sessionmanager)

add_executable(tst_ffmpegargs tst_ffmpegargs.cpp)
target_link_libraries(tst_ffmpegargs PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_ffmpegargs COMMAND tst_ffmpegargs)

add_executable(tst_settingsmanager tst_settingsmanager.cpp)
target_link_libraries(tst_settingsmanager PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_settingsmanager COMMAND tst_settingsmanager)

foreach(name tst_insta360trailer tst_insta360trim tst_insvpair tst_mp4atoms tst_outputchecks)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE trimfast_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endforeach()

# Needs TRIMFAST_TEST_VIDEO and ffmpeg (skipped otherwise).
add_executable(tst_ffmpegrunner tst_ffmpegrunner.cpp)
target_link_libraries(tst_ffmpegrunner PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_ffmpegrunner COMMAND tst_ffmpegrunner)

# Needs TRIMFAST_TEST_VIDEO (skipped otherwise); runs on a desktop session.
add_executable(tst_videoplayer tst_videoplayer.cpp)
target_link_libraries(tst_videoplayer PRIVATE trimfast_playback Qt6::Test)
add_test(NAME tst_videoplayer COMMAND tst_videoplayer)

# Widgets test; run with QT_QPA_PLATFORM=offscreen when there is no display.
add_executable(tst_timelinewidget tst_timelinewidget.cpp)
target_link_libraries(tst_timelinewidget PRIVATE trimfast_app Qt6::Test)
add_test(NAME tst_timelinewidget COMMAND tst_timelinewidget)

# Needs TRIMFAST_TEST_VIDEO and ffprobe; skipped otherwise.
add_executable(tst_mainwindow tst_mainwindow.cpp)
target_link_libraries(tst_mainwindow PRIVATE trimfast_app Qt6::Test)
add_test(NAME tst_mainwindow COMMAND tst_mainwindow)

# Needs TRIMFAST_TEST_VIDEO and ffmpeg (skipped otherwise).
add_executable(tst_exportcoordinator tst_exportcoordinator.cpp)
target_link_libraries(tst_exportcoordinator PRIVATE trimfast_core Qt6::Test)
add_test(NAME tst_exportcoordinator COMMAND tst_exportcoordinator)

# Failure paths through the real window. Needs TRIMFAST_TEST_VIDEO and ffmpeg (skipped otherwise).
add_executable(tst_failures tst_failures.cpp)
target_link_libraries(tst_failures PRIVATE trimfast_app Qt6::Test)
add_test(NAME tst_failures COMMAND tst_failures)

# Linux only (reads /proc); needs TRIMFAST_TEST_VIDEO >= 6 s.
add_executable(tst_playbackmemory tst_playbackmemory.cpp)
target_link_libraries(tst_playbackmemory PRIVATE trimfast_app Qt6::Test)
target_include_directories(tst_playbackmemory PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME tst_playbackmemory COMMAND tst_playbackmemory)

# "Open With" from the file manager arrives as a QFileOpenEvent.
add_executable(tst_application tst_application.cpp)
target_link_libraries(tst_application PRIVATE trimfast_app Qt6::Test)
add_test(NAME tst_application COMMAND tst_application)

# Command line (no window is created for these).
add_test(NAME cli_version COMMAND trimfast --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^TrimFast ${PROJECT_VERSION}")
add_test(NAME cli_help COMMAND trimfast --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: trimfast")
add_test(NAME cli_unknown_option COMMAND trimfast --no-such-option)
set_tests_properties(cli_unknown_option PROPERTIES PASS_REGULAR_EXPRESSION "unknown option")

# Version bookkeeping (scripts/version.py): the files must agree, and the script must behave.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    add_test(NAME version_consistency COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/version.py check)
    add_test(NAME version_script COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_version.py)
endif()
