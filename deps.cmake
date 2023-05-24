
include(FetchContent)

FetchContent_Declare(ziti
        GIT_REPOSITORY https://github.com/openziti/ziti-sdk-c
        GIT_TAG 0.32.6
        )
set(ZITI_BUILD_PROGRAMS off)
set(ZITI_BUILD_TESTS off)
FetchContent_MakeAvailable(ziti)