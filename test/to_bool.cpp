#include "emp-tool/emp-tool.h"
#include <iostream>

using namespace std;
using namespace emp;

struct testMe {
  uint64_t x;
  uint64_t y;
  uint32_t z;
  uint8_t t;
};

// Just testing to see if we can move some arbitrary arrays of structs to bool and back again. 
int main() {
  uint8_t len = 100;
  struct testMe structs[len];
  struct testMe output[len];
  bool b[len * 8 * sizeof(struct testMe)];
  for (uint64_t i = 0; i < len; ++i) {
    structs[i].x = i << 50;
    structs[i].y = (i + 1) << 50;
    structs[i].z = ((uint32_t) i) << 20;
    structs[i].t = (uint8_t) i;
  }
  to_bool(b, structs, len * 8 * sizeof(struct testMe));
  from_bool(b, output, len * 8 * sizeof(struct testMe));
  for (uint64_t i = 0; i < len; ++i) {
    if (output[i].x != i << 50) {
      std::cerr << "in to_bool, output[" << i << "].x was " << output[i].x << " when it should be " << (i << 50) << "\n";
      return -1;
    }
    if (output[i].y != (i + 1) << 50) {
      std::cerr << "in to_bool, output[" << i << "].y was " << output[i].y << " when it should be " << ((i + 1) << 50) << "\n";
      return -1;
    }
    if (output[i].z != ((uint32_t) i) << 20) {
      std::cerr << "in to_bool, output[" << i << "].z was " << output[i].z << " when it should be " << (((uint32_t) i) << 20) << "\n";
      return -1;
    }
    if (output[i].t != (uint8_t) i) {
      std::cerr << "in to_bool, output[" << i << "].t  was " << output[i].t << " when it should be " << ((uint8_t) i) << "\n";
      return -1;
    }
  }
  return 0;
}
