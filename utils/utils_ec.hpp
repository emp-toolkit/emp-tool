inline void bn_to_block(block * b, const bn_t bn) {
	bn_write_bin((uint8_t*)b, sizeof(block), bn);
}

inline void block_to_bn(bn_t bn, const block * b) {
	bn_read_bin(bn, (const uint8_t*)b, sizeof(block));
}
inline void initialize_relic() {
	if(initialized) return;
	initialized = true;
	if (core_init() != STS_OK) {
		core_clean();
		exit(1);
	}

	eb_param_set(EBACS_B251);
}

//TODO: fix the following.
inline block KDF(eb_t in) {
	uint8_t tmp[100];
	int eb_size= eb_size_bin(in, ECC_PACK);
	eb_write_bin(tmp, eb_size, in, ECC_PACK);
	return Hash::hash_for_block(&tmp[0], eb_size);
}

