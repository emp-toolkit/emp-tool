OPTION(CRYPTO_IN_CIRCUIT "Enable computing various crypto operations in circuit" OFF)
IF(${CRYPTO_IN_CIRCUIT})
	ADD_DEFINITIONS(-DCRYPTO_IN_CIRCUIT)
	message("Crypto in Circuit: on")
ENDIF(${CRYPTO_IN_CIRCUIT})

