add_library(gdrivesync-kde STATIC
    kdeintegration.cpp
    kwalletsecretstore.cpp
)
target_include_directories(gdrivesync-kde PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gdrivesync-kde PUBLIC gdrivesync-core Qt5::Gui KF5::Wallet KF5::KIOFileWidgets KF5::Notifications KF5::Service)
