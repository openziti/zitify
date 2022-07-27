enable_testing()
include(CTest)
find_program(PYEXE NAMES python3 python)
message("python = ${PYEXE}")
add_test(NAME zests
        COMMAND ${PYEXE} zitify_tests.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        )
set_tests_properties(zests PROPERTIES
        ENVIRONMENT "ZITIFY_LIB=$<TARGET_FILE:zitify>"
        REQUIRED_FILES "$<TARGET_FILE:zitify>"
        )