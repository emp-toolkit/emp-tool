// io/trace_io.h — an IOChannel that tees every wire byte to a pair of files,
// for diff-based wire-equivalence checks. Read example() first; the rest is
// verification.
//
// What's in trace_io.h:
//   TraceIO(under, prefix)   wrap `under` (borrowed, non-null); bytes go to
//                            <prefix>.send / <prefix>.recv, created mode 0600
//   send_data / recv_data    record then deliver (send), deliver then record (recv)
//   flush()                  flush both trace files, then `under`
//   sync()                   the IOChannel barrier; its bytes are recorded too
//   has_session_root /       forwarded to `under`, so the trace and `under`
//   next_session_id          share one sequence of session ids

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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
using namespace std;

template <class F>
static bool dies(F &&f) {
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

// An in-memory transport: sends append to `sent`, receives read `incoming`.
class MemoryIO final : public IOChannel {
public:
	explicit MemoryIO(vector<char> incoming) : incoming_(std::move(incoming)) {}

	void send_data_internal(const void *data, int64_t nbyte) override {
		const auto *bytes = static_cast<const char *>(data);
		sent.insert(sent.end(), bytes, bytes + nbyte);
	}

	void recv_data_internal(void *data, int64_t nbyte) override {
		expecting(nbyte >= 0 &&
		              offset_ + static_cast<size_t>(nbyte) <= incoming_.size(),
		          "TraceIO test: MemoryIO input exhausted");
		std::memcpy(data, incoming_.data() + offset_, static_cast<size_t>(nbyte));
		offset_ += static_cast<size_t>(nbyte);
	}

	void flush() override { ++flush_count; }

	// A test stand-in for the root a real transport agrees on at setup.
	void agree_session_root(block root) { set_session_root(root); }

	vector<char> sent;
	int flush_count = 0;

private:
	vector<char> incoming_;
	size_t offset_ = 0;
};

static vector<char> read_file(const string &path) {
	std::ifstream input(path, std::ios::binary);
	expecting(input.good(), "TraceIO test: cannot read trace file");
	return vector<char>(std::istreambuf_iterator<char>(input),
	                    std::istreambuf_iterator<char>());
}

static void make_nonprivate_file(const string &path) {
	constexpr mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP;
	int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, mode);
	expecting(fd >= 0, "TraceIO test: cannot create existing trace file");
	expecting(::fchmod(fd, mode) == 0,
	          "TraceIO test: cannot set existing trace permissions");
	expecting(::close(fd) == 0, "TraceIO test: cannot close existing trace file");
}

static bool is_private_file(const string &path) {
	struct stat info;
	return ::stat(path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600;
}

// ---------- example: typical usage ----------

static void example(const string &prefix) {
	cout << "=== example ===\n";
	MemoryIO under({'p', 'o', 'n', 'g'});
	{
		TraceIO trace(&under, prefix);
		trace.send_data("ping", 4);
		char reply[4];
		trace.recv_data(reply, 4);
		trace.flush();
		cout << "  sent \"ping\", received \"" << string(reply, 4) << "\"\n";
	}
	cout << "  " << prefix << ".send holds " << read_file(prefix + ".send").size()
	     << " bytes, .recv holds " << read_file(prefix + ".recv").size() << "\n";
}

// ---------- correctness ----------

// Bytes reach the wrapped channel unchanged, flush() leaves both trace files
// complete, and the wrapped channel is flushed once.
static bool check_delegation(const string &prefix) {
	MemoryIO under({'r', 'e', 'c', 'v'});
	TraceIO trace(&under, prefix);
	const char outbound[] = {'s', 'e', 'n', 'd'};
	char inbound[sizeof(outbound)] = {};
	trace.send_data(outbound, sizeof(outbound));
	trace.recv_data(inbound, sizeof(inbound));
	trace.flush();
	return under.sent == vector<char>(outbound, outbound + sizeof(outbound)) &&
	       std::memcmp(inbound, "recv", sizeof(inbound)) == 0 &&
	       read_file(prefix + ".send") == under.sent &&
	       read_file(prefix + ".recv") ==
	           vector<char>(inbound, inbound + sizeof(inbound)) &&
	       under.flush_count == 1;
}

// Trace files are 0600, both when created and when they already existed with
// wider permissions.
static bool check_private_files(const string &fresh, const string &existing) {
	make_nonprivate_file(existing + ".send");
	make_nonprivate_file(existing + ".recv");
	MemoryIO under({});
	{ TraceIO a(&under, fresh); }
	{ TraceIO b(&under, existing); }
	return is_private_file(fresh + ".send") && is_private_file(fresh + ".recv") &&
	       is_private_file(existing + ".send") && is_private_file(existing + ".recv");
}

// sync() runs on the transport hooks: its zero byte goes to the wrapped
// channel and into both trace files, and stays out of the byte counters.
static bool check_sync_recorded(const string &prefix) {
	MemoryIO under({'\0'});
	TraceIO trace(&under, prefix);
	trace.sync();
	trace.flush();  // sync() flushes before it receives; this flushes the .recv byte
	const vector<char> zero{'\0'};
	return under.sent == zero && read_file(prefix + ".send") == zero &&
	       read_file(prefix + ".recv") == zero && trace.send_counter == 0 &&
	       trace.recv_counter == 0;
}

static bool check_session_ids_forwarded(const string &prefix) {
	MemoryIO bare({});
	TraceIO bare_trace(&bare, prefix + "_bare");
	if (bare_trace.has_session_root()) return false;

	const block root = makeBlock(0x5e55, 0x1d);
	MemoryIO under({});
	under.agree_session_root(root);
	TraceIO trace(&under, prefix);
	// Calls through the trace and on `under` walk one sequence.
	const block got[3] = {trace.next_session_id().value(),
	                      under.next_session_id().value(),
	                      trace.next_session_id().value()};
	SessionID expected(root);
	block want[3];
	for (block &w : want) w = expected.derive().value();
	return trace.has_session_root() && cmpBlock(got, want, 3);
}

static bool run_correctness(const string &dir) {
	cout << "=== correctness ===\n";
	bool a = dies([&] { TraceIO trace(nullptr, dir + "/null"); });
	cout << "  null channel rejected          " << (a ? "OK" : "FAIL") << "\n";
	bool b = check_delegation(dir + "/delegation");
	cout << "  delegation + trace contents    " << (b ? "OK" : "FAIL") << "\n";
	bool c = check_private_files(dir + "/fresh", dir + "/existing");
	cout << "  trace files are 0600           " << (c ? "OK" : "FAIL") << "\n";
	bool d = check_sync_recorded(dir + "/sync");
	cout << "  sync() bytes recorded          " << (d ? "OK" : "FAIL") << "\n";
	bool e = check_session_ids_forwarded(dir + "/session");
	cout << "  session ids forwarded          " << (e ? "OK" : "FAIL") << "\n";
	return a && b && c && d && e;
}

int main() {
	char directory_template[] = "/tmp/emp_traceio_test.XXXXXX";
	const char *directory = ::mkdtemp(directory_template);
	expecting(directory != nullptr, "TraceIO test: cannot create temporary directory");
	const string dir(directory);
	example(dir + "/example");
	cout << "\n";
	const bool ok = run_correctness(dir);
	std::filesystem::remove_all(dir);
	if (!ok) {
		cerr << "CORRECTNESS FAILURE\n";
		return 1;
	}
	return 0;
}
