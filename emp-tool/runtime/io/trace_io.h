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
//   4. Diff each direction at its sender: both parties' `.send` files
//      must be identical across runs for the change to be wire-equivalent
//      (a `.recv` file holds only consumed bytes, so it can miss trailing
//      bytes the peer never read).
//
// Determinism contract: requires emp::is_test_mode() == true at protocol
// startup and, if the protocol spawns threads, the lane discipline of
// test_mode.h (pool tasks get lanes automatically; hand-spawned threads
// install test_lane_scope) plus one lane per traced channel — concurrent
// writers to one channel interleave nondeterministically even with
// deterministic seeds. Test-mode-off → traces are non-reproducible.

#include "emp-tool/runtime/io/io_channel.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace emp {

class TraceIO : public IOChannel {
public:
    // `under` is borrowed (not owned). `prefix` selects the trace file
    // names: "<prefix>.send" for outbound bytes, "<prefix>.recv" for
    // inbound. Files are opened binary-write, truncating, with mode 0600.
    TraceIO(IOChannel* under, const std::string& prefix)
        : under_(under) {
        expecting(under_ != nullptr, "TraceIO: underlying channel is null");
        const std::string send_path = prefix + ".send";
        const std::string recv_path = prefix + ".recv";
        send_fp_ = open_trace_file(send_path);
        recv_fp_ = open_trace_file(recv_path);
    }

    ~TraceIO() override {
        if (send_fp_ != nullptr) std::fclose(send_fp_);
        if (recv_fp_ != nullptr) std::fclose(recv_fp_);
    }

    void send_data_internal(const void* data, int64_t nbyte) override {
        expecting(nbyte >= 0,
                  "TraceIO::send_data_internal: negative byte count");
        if (nbyte == 0) return;
        // Record outbound bytes before delivering them to the transport.
        const size_t bytes = static_cast<size_t>(nbyte);
        expecting(std::fwrite(data, 1, bytes, send_fp_) == bytes,
                  "TraceIO: short write to .send");
        // Bypass under_->send_data so under_'s `send_counter` doesn't
        // double-count — we (the wrapping IOChannel) own the counter.
        under_->send_data_internal(data, nbyte);
    }

    void recv_data_internal(void* data, int64_t nbyte) override {
        expecting(nbyte >= 0,
                  "TraceIO::recv_data_internal: negative byte count");
        if (nbyte == 0) return;
        under_->recv_data_internal(data, nbyte);
        const size_t bytes = static_cast<size_t>(nbyte);
        expecting(std::fwrite(data, 1, bytes, recv_fp_) == bytes,
                  "TraceIO: short write to .recv");
    }

    void flush() override {
        flush_trace_file(send_fp_, ".send");
        flush_trace_file(recv_fp_, ".recv");
        under_->flush();
    }

private:
    static std::FILE* open_trace_file(const std::string& path) {
        int fd;
        do {
            fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
        } while (fd < 0 && errno == EINTR);
        expecting(fd >= 0, [&] {
            return "TraceIO: cannot open " + path + ": " + std::strerror(errno);
        });
        expecting(::fchmod(fd, S_IRUSR | S_IWUSR) == 0, [&] {
            return "TraceIO: cannot secure " + path + ": " + std::strerror(errno);
        });
        std::FILE* file = ::fdopen(fd, "wb");
        expecting(file != nullptr, [&] {
            return "TraceIO: fdopen failed for " + path + ": " +
                   std::strerror(errno);
        });
        return file;
    }

    static void flush_trace_file(std::FILE* file, const char* suffix) {
        expecting(std::fflush(file) == 0, [&] {
            return std::string("TraceIO: flush ") + suffix + " failed: " +
                   std::strerror(errno);
        });
    }

    IOChannel* under_;
    std::FILE* send_fp_ = nullptr;
    std::FILE* recv_fp_ = nullptr;
};

}  // namespace emp

#endif  // EMP_TRACE_IO_H__
