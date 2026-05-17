#ifndef EMP_TRACE_IO_H__
#define EMP_TRACE_IO_H__

// IOChannel adapter that tees every wire byte to a pair of files
// alongside delivering it to an underlying transport. Use to record
// protocol traces for diff-based wire-equivalent
// verification of refactors / optimizations:
//
//   1. Set EMP_TEST_MODE=1 (so all randomness is deterministic).
//   2. Run the protocol once "before" the change, dumping wire bytes
//      via TraceIO to <prefix>.send / <prefix>.recv .
//   3. Apply the optimization. Run again to a different prefix.
//   4. `diff before.send after.send` and `diff before.recv after.recv`
//      must both be empty for the change to be wire-equivalent.
//
// Determinism contract: requires single-threaded execution and
// emp::is_test_mode() == true at protocol startup. Multiple threads
// or test-mode-off → traces are non-reproducible.

#include "emp-tool/io/io_channel.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace emp {

class TraceIO : public IOChannel {
public:
    // `under` is borrowed (not owned). `prefix` selects the trace file
    // names: "<prefix>.send" for outbound bytes, "<prefix>.recv" for
    // inbound. Files are opened binary-write, truncating.
    TraceIO(IOChannel* under, const std::string& prefix)
        : under_(under) {
        const std::string send_path = prefix + ".send";
        const std::string recv_path = prefix + ".recv";
        send_fp_ = std::fopen(send_path.c_str(), "wb");
        if (send_fp_ == nullptr) {
            std::fprintf(stderr, "TraceIO: cannot open %s for write\n",
                         send_path.c_str());
            std::abort();
        }
        recv_fp_ = std::fopen(recv_path.c_str(), "wb");
        if (recv_fp_ == nullptr) {
            std::fprintf(stderr, "TraceIO: cannot open %s for write\n",
                         recv_path.c_str());
            std::abort();
        }
    }

    ~TraceIO() override {
        if (send_fp_ != nullptr) std::fclose(send_fp_);
        if (recv_fp_ != nullptr) std::fclose(recv_fp_);
    }

    void send_data_internal(const void* data, int64_t nbyte) override {
        // Tee first, deliver after, so a crash mid-write still leaves
        // a trace prefix that matches what the peer didn't yet see.
        if ((int64_t)std::fwrite(data, 1, nbyte, send_fp_) != nbyte) {
            std::fprintf(stderr, "TraceIO: short write to .send\n");
            std::abort();
        }
        // Bypass under_->send_data so under_'s `counter` doesn't
        // double-count — we (the wrapping IOChannel) own the counter.
        under_->send_data_internal(data, nbyte);
    }

    void recv_data_internal(void* data, int64_t nbyte) override {
        under_->recv_data_internal(data, nbyte);
        if ((int64_t)std::fwrite(data, 1, nbyte, recv_fp_) != nbyte) {
            std::fprintf(stderr, "TraceIO: short write to .recv\n");
            std::abort();
        }
    }

    void flush() override { under_->flush(); }
    void sync()  override { under_->sync(); }

private:
    IOChannel* under_;
    std::FILE* send_fp_ = nullptr;
    std::FILE* recv_fp_ = nullptr;
};

}  // namespace emp

#endif  // EMP_TRACE_IO_H__
