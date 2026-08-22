#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include "emp-tool/emp-tool.h"

using namespace emp;

template <class F>
static bool dies(F&& f) {
	pid_t pid = ::fork();
	expecting(pid >= 0, "TraceIO test: fork failed");
	if (pid == 0) {
		std::freopen("/dev/null", "w", stderr);
		f();
		::_exit(0);
	}
	int status = 0;
	::waitpid(pid, &status, 0);
	return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

class MemoryIO final : public IOChannel {
public:
	explicit MemoryIO(std::vector<char> incoming)
	    : incoming_(std::move(incoming)) {}

	void send_data_internal(const void* data, int64_t nbyte) override {
		const auto* bytes = static_cast<const char*>(data);
		sent.insert(sent.end(), bytes, bytes + nbyte);
	}

	void recv_data_internal(void* data, int64_t nbyte) override {
		expecting(nbyte >= 0 && offset_ + static_cast<size_t>(nbyte) <= incoming_.size(),
		          "TraceIO test: MemoryIO input exhausted");
		std::memcpy(data, incoming_.data() + offset_, static_cast<size_t>(nbyte));
		offset_ += static_cast<size_t>(nbyte);
	}

	void flush() override { ++flush_count; }

	std::vector<char> sent;
	int flush_count = 0;

private:
	std::vector<char> incoming_;
	size_t offset_ = 0;
};

static std::vector<char> read_file(const std::string& path) {
	std::ifstream input(path, std::ios::binary);
	expecting(input.good(), "TraceIO test: cannot read trace file");
	return std::vector<char>(std::istreambuf_iterator<char>(input),
	                         std::istreambuf_iterator<char>());
}

static void make_nonprivate_file(const std::string& path) {
	constexpr mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP;
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
	expecting(fd >= 0, "TraceIO test: cannot create existing trace file");
	expecting(::fchmod(fd, mode) == 0,
	          "TraceIO test: cannot set existing trace permissions");
	expecting(::close(fd) == 0,
	          "TraceIO test: cannot close existing trace file");
}

static void expect_private_file(const std::string& path) {
	struct stat info;
	expecting(::stat(path.c_str(), &info) == 0,
	          "TraceIO test: cannot stat trace file");
	expecting((info.st_mode & 0777) == 0600,
	          "TraceIO test: trace file permissions are not 0600");
}

int main() {
	char directory_template[] = "/tmp/emp_traceio_test.XXXXXX";
	char* directory = ::mkdtemp(directory_template);
	expecting(directory != nullptr,
	          "TraceIO test: cannot create temporary directory");
	const std::string prefix = std::string(directory) + "/trace";
	const std::string send_path = prefix + ".send";
	const std::string recv_path = prefix + ".recv";

	expecting(dies([&] { TraceIO trace(nullptr, prefix + ".null"); }),
	          "TraceIO test: null underlying channel was accepted");

	make_nonprivate_file(send_path);
	make_nonprivate_file(recv_path);
	MemoryIO under({'r', 'e', 'c', 'v'});
	{
		TraceIO trace(&under, prefix);
		const char outbound[] = {'s', 'e', 'n', 'd'};
		char inbound[sizeof(outbound)] = {};
		trace.send_data(outbound, sizeof(outbound));
		trace.recv_data(inbound, sizeof(inbound));
		trace.flush();

		expecting(under.sent == std::vector<char>(outbound, outbound + sizeof(outbound)),
		          "TraceIO test: outbound delegation mismatch");
		expecting(std::memcmp(inbound, "recv", sizeof(inbound)) == 0,
		          "TraceIO test: inbound delegation mismatch");
		expecting(read_file(send_path) == under.sent,
		          "TraceIO test: .send was not flushed");
		expecting(read_file(recv_path) == std::vector<char>(inbound, inbound + sizeof(inbound)),
		          "TraceIO test: .recv was not flushed");
		expecting(under.flush_count == 1,
		          "TraceIO test: underlying flush was not delegated");
		expect_private_file(send_path);
		expect_private_file(recv_path);
	}

	expecting(::unlink(send_path.c_str()) == 0,
	          "TraceIO test: cannot remove .send");
	expecting(::unlink(recv_path.c_str()) == 0,
	          "TraceIO test: cannot remove .recv");
	expecting(::rmdir(directory) == 0,
	          "TraceIO test: cannot remove temporary directory");
}
