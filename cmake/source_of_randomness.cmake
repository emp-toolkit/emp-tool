OPTION(USE_RANDOM_DEVICE "Option description" OFF)

# Use rdseed if available
unset(RDSEED_COMPILE_RESULT CACHE)
unset(RDSEED_RUN_RESULT CACHE)
file(WRITE ${CMAKE_SOURCE_DIR}/rdseedtest.c "#include <stdio.h>\n#include <x86intrin.h>\nint main(){\nunsigned long long r;\n_rdseed64_step(&r);\nreturn 0;\n}\n")
try_run(RDSEED_RUN_RESULT RDSEED_COMPILE_RESULT ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/rdseedtest.c CMAKE_FLAGS ${CMAKE_C_FLAGS})
file(REMOVE ${CMAKE_SOURCE_DIR}/rdseedtest.c)

IF(NOT ${RDSEED_COMPILE_RESULT})
        set(USE_RANDOM_DEVICE ON)
ELSE(NOT ${RDSEED_COMPILE_RESULT})
	string(COMPARE EQUAL "${RDSEED_RUN_RESULT}" "0" RDSEED_RUN_SUCCESS)
        IF(NOT ${RDSEED_RUN_SUCCESS})
                set(USE_RANDOM_DEVICE ON)
        ENDIF(NOT ${RDSEED_RUN_SUCCESS})
ENDIF(NOT ${RDSEED_COMPILE_RESULT})

IF(${USE_RANDOM_DEVICE})
	ADD_DEFINITIONS(-DEMP_USE_RANDOM_DEVICE)
	set(EMP_USE_RANDOM_DEVICE ON) # for configure_file below
	message("${Red}-- Source of Randomness: random_device${ColourReset}")
ELSE(${USE_RANDOM_DEVICE})
	message("${Green}-- Source of Randomness: rdseed${ColourReset}")
	set(EMP_USE_RANDOM_DEVICE OFF) # for configure_file below
ENDIF(${USE_RANDOM_DEVICE})

configure_file(${CMAKE_SOURCE_DIR}/emp-tool/definitions/definitions.h.in ${CMAKE_SOURCE_DIR}/emp-tool/definitions/definitions.h @ONLY)
