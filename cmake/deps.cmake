
include(FetchContent)

FetchContent_Declare(ziti
        GIT_REPOSITORY https://github.com/openziti/ziti-sdk-c
        GIT_TAG 1.9.15
        )
set(ZITI_BUILD_PROGRAMS off)
set(ZITI_BUILD_TESTS off)
FetchContent_MakeAvailable(ziti)

# Add C-ares for DNS resolution hooking
FetchContent_Declare(c-ares
        GIT_REPOSITORY https://github.com/c-ares/c-ares
        GIT_TAG cares-1_28_1
        )
set(CARES_BUILD_TESTS off)
set(CARES_BUILD_TOOLS off)
FetchContent_MakeAvailable(c-ares)