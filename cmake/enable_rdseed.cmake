OPTION(ENABLE_RDSEED "Use RDSEED for True Randomness" ON)

# Use rdseed if available
unset(RDSEED_COMPILE_RESULT CACHE)
unset(RDSEED_RUN_RESULT CACHE)
file(WRITE ${CMAKE_SOURCE_DIR}/rdseedtest.c "#include <stdio.h>\n#include <x86intrin.h>\nint main(){\nunsigned long long r;\n_rdseed64_step(&r);\nreturn 0;\n}\n")
try_run(RDSEED_RUN_RESULT RDSEED_COMPILE_RESULT ${CMAKE_BINARY_DIR} ${CMAKE_SOURCE_DIR}/rdseedtest.c CMAKE_FLAGS ${CMAKE_C_FLAGS})
file(REMOVE ${CMAKE_SOURCE_DIR}/rdseedtest.c)
string(COMPARE EQUAL "${RDSEED_RUN_RESULT}" "0" RDSEED_RUN_SUCCESS)

IF(NOT ${RDSEED_COMPILE_RESULT} OR NOT ${RDSEED_RUN_SUCCESS})
	set(ENABLE_RDSEED OFF)
ENDIF(NOT ${RDSEED_COMPILE_RESULT} OR NOT ${RDSEED_RUN_SUCCESS})

IF(${ENABLE_RDSEED})
	ADD_DEFINITIONS(-DEMP_ENABLE_RDSEED)
	message("${Green}-- Source of Randomness: rdseed${ColourReset}")
ELSE(${ENABLE_RDSEED})
	message("${Red}-- Source of Randomness: random_device${ColourReset}")
ENDIF(${ENABLE_RDSEED})

