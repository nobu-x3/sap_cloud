include(FetchContent)

FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(sap_drive_tests
    drive_tests.cpp
)

target_link_libraries(sap_drive_tests
    PRIVATE
        sap::drive_lib
        sap::fs
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(sap_drive_tests)
