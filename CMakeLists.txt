cmake_minimum_required(VERSION 3.28)

include(cmake/ReadVersion.cmake)

project(kubewatch VERSION ${VERSION_STRING})

# Export version to source code (used by UpdateChecker to compare against version.txt
# published on GitHub Pages)
configure_file(${CMAKE_SOURCE_DIR}/cmake/version.h.in ${CMAKE_SOURCE_DIR}/include/Version.h)

# Generate installer config/package XML from their .in templates
file(READ "${CMAKE_SOURCE_DIR}/installer/config/config.xml.in" _CONFIG_XML)
string(REPLACE "@VERSION_STRING@" "${VERSION_STRING}" _CONFIG_XML "${_CONFIG_XML}")
file(WRITE "${CMAKE_SOURCE_DIR}/installer/config/config.xml" "${_CONFIG_XML}")

string(TIMESTAMP _RELEASE_DATE "%Y-%m-%d")
file(READ "${CMAKE_SOURCE_DIR}/installer/packages/de.jensvogt.kubewatch/meta/package.xml.in" _PACKAGE_XML)
string(REPLACE "@VERSION_STRING@" "${VERSION_STRING}" _PACKAGE_XML "${_PACKAGE_XML}")
string(REPLACE "@RELEASE_DATE@" "${_RELEASE_DATE}" _PACKAGE_XML "${_PACKAGE_XML}")
file(WRITE "${CMAKE_SOURCE_DIR}/installer/packages/de.jensvogt.kubewatch/meta/package.xml" "${_PACKAGE_XML}")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)


find_package(Qt6 COMPONENTS
        Core
        Gui
        Widgets
        Svg
        Network
        REQUIRED)

if (WIN32)
    set(APP_ICON_RESOURCE resources/kubewatch.rc)
endif ()

add_executable(kubewatch WIN32 MACOSX_BUNDLE
        main.cpp
        resources/awsmock-qt-ui.qrc
        ${APP_ICON_RESOURCE}
        include/components/PageableTable.h
        src/components/PageableTable.cpp
        src/components/PageableTable.ui
        include/utils/Configuration.h
        src/utils/Configuration.cpp
        include/utils/IconUtils.h
        src/utils/IconUtils.cpp
        include/utils/PrefixFilterModel.h
        src/utils/PrefixFilterModel.cpp
        include/utils/EventBus.h
        include/utils/Logging.h
        include/onelogin/OneLoginAuth.h
        src/onelogin/OneLoginAuth.cpp
        include/Version.h
        include/utils/UpdateChecker.h
)
target_include_directories(kubewatch PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kubewatch
        Qt::Core
        Qt::Gui
        Qt::Widgets
        Qt::Svg
        Qt::Network
)

if (WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(DEBUG_SUFFIX)
    if (MSVC AND CMAKE_BUILD_TYPE MATCHES "Debug")
        set(DEBUG_SUFFIX "d")
    endif ()
    set(QT_INSTALL_PATH "${CMAKE_PREFIX_PATH}")
    if (NOT QT_INSTALL_PATH AND Qt6_DIR)
        set(QT_INSTALL_PATH "${Qt6_DIR}/../../..")
    endif ()
    if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
        set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        if (NOT EXISTS "${QT_INSTALL_PATH}/bin")
            set(QT_INSTALL_PATH "${QT_INSTALL_PATH}/..")
        endif ()
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/platforms/qwindows${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/platforms/")
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/iconengines/qsvgicon${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/iconengines/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/iconengines/qsvgicon${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/iconengines/")
    endif ()
    if (EXISTS "${QT_INSTALL_PATH}/plugins/tls/qschannelbackend${DEBUG_SUFFIX}.dll")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/tls/")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/plugins/tls/qschannelbackend${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/plugins/tls/")
    endif ()
    foreach (QT_LIB Core Gui Widgets Svg Network)
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy
                "${QT_INSTALL_PATH}/bin/Qt6${QT_LIB}${DEBUG_SUFFIX}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>")
    endforeach (QT_LIB)
endif ()
