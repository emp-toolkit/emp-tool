#ifndef EMP_ABANDON_IO_CHANNEL_H__
#define EMP_ABANDON_IO_CHANNEL_H__
#include "emp-tool/io/io_channel.h"

namespace emp {
// Essentially drop all communication
class AbandonIO: public IOChannel<AbandonIO> { public:
	int size = 0;
	void send_data(const void * data, int len) {
		size += len;
	}

	void recv_data(void  * data, int len) {
	}
};
}
#endif
