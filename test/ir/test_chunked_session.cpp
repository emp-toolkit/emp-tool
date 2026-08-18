// ir/session/chunked_session.h — shared typed boundary for chunk-recording
// protocol sessions. Read main() first; the rest is a protocol-free test double.
//
// What's in chunked_session.h:
//   input<V>(...)              authenticate a fixed-width typed input
//   reveal(...)                flush and decode, with empty = non-recipient
//   run_artifact<RetV>(...)    debug-validate and execute a raw BooleanProgram


#include "emp-tool/ir/session/chunked_session.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

using namespace emp;

template <class Ctx, int W>
struct SmallValue {
  using Wire = typename Ctx::Wire;
  using context_type = Ctx;
  using clear_t = uint8_t;
  template <class C2> using rebind = SmallValue<C2, W>;

  Ctx* ctx = nullptr;
  std::array<Wire, W> wires{};

  Ctx* context() const { return ctx; }
  static constexpr int width() { return W; }
  void pack_wires(Wire* out) const {
    for (int i = 0; i < W; ++i) out[i] = wires[(std::size_t)i];
  }
  static SmallValue from_wires(Ctx& c, const Wire* in) {
    SmallValue out;
    out.ctx = &c;
    for (int i = 0; i < W; ++i) out.wires[(std::size_t)i] = in[i];
    return out;
  }
  static std::array<bool, (std::size_t)W> encode(clear_t value) {
    std::array<bool, (std::size_t)W> out{};
    for (int i = 0; i < W; ++i) out[(std::size_t)i] = ((value >> i) & 1) != 0;
    return out;
  }
  static clear_t decode(const bool* bits) {
    clear_t out = 0;
    for (int i = 0; i < W; ++i) out |= (clear_t)(bits[i] ? 1u << i : 0);
    return out;
  }
};

class TestChunkedSession : public ChunkedSession<TestChunkedSession> {
public:
  std::vector<uint8_t> decoded;
  int program_runs = 0;

  void authenticate_into_(int, const std::vector<uint8_t>&,
                          const std::vector<uint32_t>& ids) {
    carried_.insert(ids.begin(), ids.end());
  }
  void batch_authenticate_into_(const std::vector<int>&,
                                const std::vector<std::vector<uint8_t>>&,
                                const std::vector<std::vector<uint32_t>>& id_lists) {
    for (const auto& ids : id_lists) carried_.insert(ids.begin(), ids.end());
  }
  void run_program_into_(const circuit::BooleanProgram&,
                         const std::vector<uint32_t>&,
                         const std::vector<uint32_t>& out_ids) {
    ++program_runs;
    carried_.insert(out_ids.begin(), out_ids.end());
  }
  std::vector<uint8_t> decode_(const std::vector<uint32_t>&, int) {
    return decoded;
  }
  void validate_recipient_(int recipient) {
    expecting(recipient == PUBLIC || recipient > 0,
              "TestChunkedSession: invalid recipient");
  }
  bool is_materialized_(uint32_t id) const {
    return carried_.count(id) != 0;
  }
  template <class Pred>
  void prune_carried_(Pred&& keep) {
    for (auto it = carried_.begin(); it != carried_.end();) {
      if (!keep(*it)) it = carried_.erase(it);
      else ++it;
    }
  }
private:
  std::unordered_set<uint32_t> carried_;
};

template <class F>
static bool dies(F&& f) {
  pid_t pid = fork();
  if (pid == 0) {
    int dn = open("/dev/null", O_WRONLY);
    if (dn >= 0) dup2(dn, 2);
    f();
    _exit(0);
  }
  int status = 0;
  waitpid(pid, &status, 0);
  return !(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

int main() {
  using Pair = SmallValue<ChunkRecorderCtx, 2>;
  using Bit = SmallValue<ChunkRecorderCtx, 1>;
  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::printf("  BAD: %s\n", what);
      ++failures;
    }
  };

  {
    TestChunkedSession sess;
    Pair value = sess.input<Pair>(1, 2);

    sess.decoded.clear();
    check(!sess.reveal(value, PUBLIC).has_value(),
          "empty decode means this party is not a recipient");

    sess.decoded = {1, 1};
    auto opened = sess.reveal(value, PUBLIC);
    check(opened.has_value() && *opened == 3,
          "an exact-width decode reaches the value codec");
  }

  check(dies([] {
    TestChunkedSession sess;
    Pair value = sess.input<Pair>(1, 0);
    sess.decoded = {1};
    (void)sess.reveal(value, PUBLIC);
  }), "a nonempty wrong-width decode is a backend error");

#ifndef NDEBUG
  check(dies([] {
    TestChunkedSession sess;
    Pair input = sess.input<Pair>(1, 0);
    circuit::BooleanProgram bad;
    bad.num_inputs = 2;
    bad.num_wires = 3;
    bad.gates = {{0, 1, 2, circuit::Op::Xor}};
    bad.outputs = {9};
    (void)sess.run_artifact<Bit>(bad, input);
  }), "run_artifact debug-validates a raw program before backend execution");
#endif

  std::printf("test_chunked_session: %s\n", failures == 0 ? "GOOD!" : "BAD!");
  return failures == 0 ? 0 : 1;
}
