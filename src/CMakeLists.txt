add_subdirectory(core)
add_subdirectory(kde)
add_subdirectory(gui)

add_executable(gdrive-sync main.cpp icon.qrc)
target_compile_definitions(gdrive-sync PRIVATE GDS_VERSION="${PROJECT_VERSION}")
target_link_libraries(gdrive-sync PRIVATE
    gdrivesync-gui gdrivesync-kde
    KF5::CoreAddons KF5::DBusAddons KF5::I18n)
install(TARGETS gdrive-sync ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
