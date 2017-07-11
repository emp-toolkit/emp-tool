
#ifndef ABANDON_IO_CHANNEL
#define ABANDON_IO_CHANNEL
#include "io_channel.h"

/** @addtogroup IO
  @{
 */

// Essentially drop all communication
class AbandonIO: public IOChannel<AbandonIO> { public:
	int size = 0;
	void send_data_impl(const void * data, int len) {
		size += len;
	}

	void recv_data_impl(void  * data, int len) {
	}
};
/**@}*/
#endif
