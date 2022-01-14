OPTION(THREADING "Enable multi-threaded backend" OFF)
IF(THREADING)
	ADD_DEFINITIONS(-DTHREADING)
	message("Multi Threading: on")
ENDIF(THREADING)

