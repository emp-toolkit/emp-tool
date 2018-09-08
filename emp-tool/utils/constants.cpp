#include "emp-tool/io/mem_io_channel.h"
#include "emp-tool/utils/constants.h"
#include "emp-tool/utils/coot_table.h"
#include <mutex>
namespace emp { 
namespace gTbl {
    static std::mutex init_guard;
    static bool initialized = false;
    void init() {
        init_guard.lock();
        if (!initialized) {
            initialize_relic();
            MemIO mio;
            char *tmp = mio.buffer;
            mio.buffer = (char *) eb_curve_get_tab_data;
            mio.size = 15400 * 8;
            mio.recv_eb(tbl, RELIC_EB_TABLE_MAX);
            eb_new(C);
            mio.buffer = tmp;
            initialized = true;
        }
        init_guard.unlock();
    }
}}

