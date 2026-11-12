add_library(gdrivesync-core STATIC
    appcore.cpp
    configwriter.cpp
    driveclient.cpp
    foldermanager.cpp
    format.cpp
    http.cpp
    localscanner.cpp
    logger.cpp
    oauthflow.cpp
    pkce.cpp
    planner.cpp
    scheduler.cpp
    syncengine.cpp
    syncstate.cpp
    tokensource.cpp
    secretstore.h
)
target_include_directories(gdrivesync-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gdrivesync-core PUBLIC Qt5::Core Qt5::Network Qt5::Concurrent KF5::ConfigCore)
