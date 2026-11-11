cmake_minimum_required(VERSION 3.22)
project(gdrive-sync VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(QT_MIN_VERSION "5.15.0")
set(KF_MIN_VERSION "5.92.0")

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(FeatureSummary)

option(GDS_WERROR "Treat compiler warnings as errors" OFF)
option(GDS_SANITIZE "Build with AddressSanitizer and UBSan" OFF)

find_package(Qt5 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Network Concurrent Widgets Test)
find_package(KF5 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    Config CoreAddons DBusAddons I18n KIO Notifications Wallet WidgetsAddons)

set(CMAKE_AUTOMOC ON)

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # triggered by Qt's own macros (QSKIP, QVERIFY2, ...)
    add_compile_options(-Wno-gnu-zero-variadic-macro-arguments)
endif()
add_compile_definitions(QT_NO_CAST_FROM_ASCII QT_NO_CAST_TO_ASCII QT_NO_URL_CAST_FROM_STRING
    QT_NO_KEYWORDS QT_STRICT_ITERATORS TRANSLATION_DOMAIN="gdrive-sync")
if(GDS_WERROR)
    add_compile_options(-Werror)
endif()
if(GDS_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

install(FILES data/io.github.anjankow.gdrivesync.desktop DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/gdrive-sync.notifyrc DESTINATION ${KDE_INSTALL_KNOTIFYRCDIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
