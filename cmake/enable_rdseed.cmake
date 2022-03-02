OPTION(USE_RANDOM_DEVICE "Enable RDSEED instruction" OFF)

include(CheckCSourceRuns)

# Use rdseed if available
unset(RDSEED_COMPILE_RESULT CACHE)
unset(RDSEED_RUN_RESULT CACHE)
set(CMAKE_REQUIRED_FLAGS "-mrdseed") # in case CMAKE_C_FLAGS doesn't have it, ex. if add_compile_options is used instead
check_c_source_runs("#include <x86intrin.h>\nint main(){\nunsigned long long r;\n_rdseed64_step(&r);\nreturn 0;\n}\n" RDSEED_RUN_RESULT)

string(COMPARE EQUAL "${RDSEED_RUN_RESULT}" "1" RDSEED_RUN_SUCCESS)
IF(NOT ${RDSEED_RUN_SUCCESS})
	set(USE_RANDOM_DEVICE ON)
ENDIF(NOT ${RDSEED_RUN_SUCCESS})

IF(${USE_RANDOM_DEVICE})
	ADD_DEFINITIONS(-DEMP_ENABLE_RDSEED)
	message("${Red}-- Source of Randomness: random_device${ColourReset}")
ELSE(${USE_RANDOM_DEVICE})
	message("${Green}-- Source of Randomness: rdseed${ColourReset}")
ENDIF(${USE_RANDOM_DEVICE})
