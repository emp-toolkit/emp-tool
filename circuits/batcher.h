#ifndef BATCHER_H__
#define BATCHER_H__
#include <vector>
#include "backend.h"
using std::vector;

class Batcher { public:
	vector<bool> values;
	vector<int> lens;
	int len_ptr = 0;
	block * labels = nullptr;
	block * label_ptr = nullptr;
	void add(bool * b, int length) {
		lens.push_back(length);
		for(int i = 0; i < length; ++i)
			values.push_back(b[i]);
	}

	~Batcher(){
		if(labels != nullptr)
			delete[] labels;
	}

	template<typename T, typename... Args>
	void add(Args&&... args) {
		int length = T::bool_size(std::forward<Args>(args)...);
		bool * tmp = new bool[length];
		T::bool_data(tmp, std::forward<Args>(args)...);
		add(tmp, length);
		delete []tmp;
	}

	void to_bool(bool * b) {
		for(size_t i = 0; i < values.size(); ++i){
			b[i] = values[i];
		}
	}
	
	void make_semi_honest(int party, Backend * be = nullptr) {
		if(be == nullptr)
			be = local_backend;
		bool * bools = new bool[size()];
		to_bool(bools);
		label_ptr = labels = new block[size()];
		local_backend->Feed(labels, party, bools, size());
		len_ptr = 0;
		delete[] bools;
	}

	void set_blocks(block * b) {
		this->labels = b;
		this->label_ptr = b;
		len_ptr = 0;
	}

	template<typename T>
	T next() {
		T res = T(lens[len_ptr], label_ptr);
		label_ptr +=lens[len_ptr];
		len_ptr ++;
		return res;
	}

	int size() {
		return values.size();
	}
};
#endif