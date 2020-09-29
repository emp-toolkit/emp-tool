#ifndef EMP_ABANDON_IO_CHANNEL_H__
#define EMP_ABANDON_IO_CHANNEL_H__
#include "emp-tool/io/io_channel.h"

namespace emp {
// Essentially drop all communication
class AbandonIO: public IOChannel<AbandonIO> { public:
	void send_data_internal(const void * data, int len) {
	}

	void recv_data_internal(void  * data, int len) {
	}
};
}
#endif
