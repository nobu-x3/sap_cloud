cmake_minimum_required(VERSION 3.20)
project(sap_drive
    VERSION 0.1.0
    DESCRIPTION "SapCloud file storage and sync server"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SAP_DRIVE_BUILD_TESTS "Build sap_drive tests" ${PROJECT_IS_TOP_LEVEL})

add_subdirectory(sap_core)
add_subdirectory(sap_fs)
add_subdirectory(sap_db)
add_subdirectory(sap_http)
add_subdirectory(sap_sync)
add_subdirectory(tomlplusplus)

add_library(sap_drive_lib STATIC
    src/config.cpp
    src/storage/metadata.cpp
    src/services/file_service.cpp
    src/services/notes_service.cpp
)
add_library(sap::drive_lib ALIAS sap_drive_lib)

target_include_directories(sap_drive_lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sap_core/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sap_db/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sap_fs/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sap_https/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sap_sync/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/tomlplusplus/include>
        $<INSTALL_INTERFACE:include>
        $<INSTALL_INTERFACE:sap_core/include>
        $<INSTALL_INTERFACE:sap_db/include>
        $<INSTALL_INTERFACE:sap_fs/include>
        $<INSTALL_INTERFACE:sap_https/include>
        $<INSTALL_INTERFACE:sap_sync/include>
        $<INSTALL_INTERFACE:tomlplusplus/include>
)

target_link_libraries(sap_drive_lib
    PUBLIC
        # sap::core
        sap::db
        sap::fs
        sap::http
        sap::sync
        tomlplusplus::tomlplusplus
)

target_compile_features(sap_drive_lib PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(sap_drive_lib PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(sap_drive_lib PRIVATE /W4)
endif()

# Main executable
add_executable(sap_drive
    src/main.cpp
)

target_link_libraries(sap_drive PRIVATE sap_drive_lib)

if(SAP_DRIVE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

install(TARGETS sap_drive
    RUNTIME DESTINATION bin
)