OPTION(ENABLE_FLOAT "Enable Float circuits" OFF)

IF(${ENABLE_FLOAT})
message("${Green}-- Enable Float Circuit: On${ColourReset}")
set(sources
${sources}
emp-tool/circuits/float32_add.cpp
emp-tool/circuits/float32_cos.cpp
emp-tool/circuits/float32_div.cpp
emp-tool/circuits/float32_eq.cpp
emp-tool/circuits/float32_le.cpp
emp-tool/circuits/float32_leq.cpp
emp-tool/circuits/float32_mul.cpp
emp-tool/circuits/float32_sin.cpp
emp-tool/circuits/float32_sq.cpp
emp-tool/circuits/float32_sqrt.cpp
emp-tool/circuits/float32_sub.cpp
emp-tool/circuits/float32_exp2.cpp
emp-tool/circuits/float32_exp.cpp
emp-tool/circuits/float32_ln.cpp
emp-tool/circuits/float32_log2.cpp
)
ELSE(${ENABLE_FLOAT})
message("${Red}-- Enable Float Circuit: Off${ColourReset}")
ENDIF(${ENABLE_FLOAT})

