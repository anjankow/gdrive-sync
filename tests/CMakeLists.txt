add_library(gdstest-support STATIC mock/mockgoogleserver.cpp testutil.h)
target_include_directories(gdstest-support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gdstest-support PUBLIC gdrivesync-core Qt5::Test)

function(gds_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE gdstest-support ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        TIMEOUT 180
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;LC_ALL=C.UTF-8")
endfunction()

gds_add_test(tst_planner)
gds_add_test(tst_driveclient)
gds_add_test(tst_account)
