add_library(gdrivesync-gui STATIC
    dialogs.cpp
    mainwindow.cpp
)
target_include_directories(gdrivesync-gui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gdrivesync-gui PUBLIC gdrivesync-core Qt5::Widgets KF5::I18n KF5::WidgetsAddons)
