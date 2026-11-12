add_library(gdstest-support STATIC mock/mockgoogleserver.cpp testutil.h)
target_include_directories(gdstest-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gdstest-support PUBLIC gdrivesync-core Qt5::Test)

# Keep the tests away from the real home directory (trash, config, places).
set(GDS_TEST_XDG ${CMAKE_BINARY_DIR}/test-xdg)
file(MAKE_DIRECTORY ${GDS_TEST_XDG}/data ${GDS_TEST_XDG}/config ${GDS_TEST_XDG}/cache)

function(gds_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE gdstest-support ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8;XDG_DATA_HOME=${GDS_TEST_XDG}/data;XDG_CONFIG_HOME=${GDS_TEST_XDG}/config;XDG_CACHE_HOME=${GDS_TEST_XDG}/cache")
endfunction()

gds_add_test(tst_planner)
gds_add_test(tst_driveclient)
gds_add_test(tst_account)
gds_add_test(tst_sync)
gds_add_test(tst_deletions)
gds_add_test(tst_progress)
gds_add_test(tst_folders)
gds_add_test(tst_autosync)
gds_add_test(tst_logging)
gds_add_test(tst_settings)
gds_add_test(tst_icons Qt5::Gui)
target_compile_definitions(tst_icons PRIVATE GDS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
gds_add_test(tst_gui gdrivesync-gui)
gds_add_test(tst_responsiveness gdrivesync-gui)
gds_add_test(tst_places gdrivesync-kde)
gds_add_test(tst_live_drive)
