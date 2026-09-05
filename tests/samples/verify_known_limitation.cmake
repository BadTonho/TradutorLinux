if(NOT DEFINED FIXTURE OR NOT DEFINED LIMITATION OR NOT DEFINED MATRIX)
    message(FATAL_ERROR "FIXTURE, LIMITATION and MATRIX are required")
endif()

if(NOT EXISTS "${MATRIX}")
    message(FATAL_ERROR "Compatibility matrix was not found: ${MATRIX}")
endif()
file(READ "${MATRIX}" matrix_content)

string(FIND "${matrix_content}" "${FIXTURE}" fixture_position)
if(fixture_position EQUAL -1)
    message(FATAL_ERROR
        "${FIXTURE} has a known limitation but is missing from the compatibility matrix"
    )
endif()

string(SUBSTRING "${matrix_content}" 0 "${fixture_position}" before_fixture)
string(FIND "${before_fixture}" "\n" previous_newline REVERSE)
if(previous_newline EQUAL -1)
    set(line_start 0)
else()
    math(EXPR line_start "${previous_newline} + 1")
endif()
string(SUBSTRING "${matrix_content}" "${line_start}" -1 from_entry)
string(FIND "${from_entry}" "\n" next_newline)
if(next_newline EQUAL -1)
    string(LENGTH "${from_entry}" entry_length)
else()
    set(entry_length "${next_newline}")
endif()
string(SUBSTRING "${from_entry}" 0 "${entry_length}" compatibility_entry)

string(FIND "${compatibility_entry}" "${LIMITATION}" limitation_position)
if(limitation_position EQUAL -1)
    message(FATAL_ERROR
        "${FIXTURE} expectation '${LIMITATION}' is missing from its compatibility entry"
    )
endif()

message(STATUS "Known limitation '${LIMITATION}' for ${FIXTURE} is documented")
