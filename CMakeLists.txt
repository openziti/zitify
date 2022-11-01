cmake_minimum_required(VERSION 3.14)
include(cmake/version.cmake)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

get_version(${CMAKE_CURRENT_LIST_DIR}/version.txt zitify_version)

project(zitify
        VERSION ${zitify_version}
        LANGUAGES C CXX
        )

message("zitify version: ${PROJECT_VERSION}")
include(deps.cmake)

include(CTest)
enable_testing()

find_program(ZIP NAMES 7z)
if (ZIP)
    set(ZIP_OPTS a -tzip)
else ()
    find_program(ZIP NAMES zip)
    if (ZIP)
        set(ZIP_OPTS "-jv")
    else ()
        message("zip program not found")
    endif ()
endif ()

if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

    add_library(zitify SHARED zitify.cpp)
    set_target_properties(zitify PROPERTIES
                      VERSION ${PROJECT_VERSION}
                      SOVERSION ${PROJECT_VERSION_MAJOR})

    target_link_libraries(zitify PRIVATE ziti)

    set(zitify_archive zitify-${CMAKE_PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}.zip)

    add_custom_target(zitify-pack
            BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${zitify_archive}
            ALL
            DEPENDS zitify
            COMMAND ${ZIP} ${ZIP_OPTS} ${zitify_archive} $<TARGET_FILE:zitify> ${CMAKE_CURRENT_SOURCE_DIR}/zitify
            )

    add_subdirectory(tests)
else()
    add_custom_target(zitify-pack
            COMMAND echo "zitify not yet implemented on ${CMAKE_SYSTEM_NAME} platform"
            )

endif ()