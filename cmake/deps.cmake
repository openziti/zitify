
include(FetchContent)

set(ZITI_BUILD_PROGRAMS off)
set(ZITI_BUILD_TESTS off)

set(ZITI_SDK_VERSION "1.9.5" CACHE STRING "Version of Ziti SDK to use")

if (ZITI_SDK_DIR)
        add_subdirectory(${ZITI_SDK_DIR} ${CMAKE_CURRENT_BINARY_DIR}/ziti-sdk)
else()
        FetchContent_Declare(ziti
                GIT_REPOSITORY https://github.com/openziti/ziti-sdk-c
                GIT_TAG ${ZITI_SDK_VERSION}
        )
        FetchContent_MakeAvailable(ziti)
endif ()