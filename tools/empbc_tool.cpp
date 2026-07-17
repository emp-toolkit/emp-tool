// empbc-tool — inspect, compare, and integrity-check native .empbc assets.

#include "emp-tool/ir/analysis.h"
#include "emp-tool/ir/empbc.h"
#include "emp-tool/runtime/core/error.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;
using emp::expecting;
using emp::circuit::BooleanProgram;
using emp::circuit::ProgramStats;
using emp::circuit::analyze_program;
using emp::circuit::load_empbc;
using emp::circuit::wire_reuse_name;

namespace {

constexpr uint64_t kMaxFileBytes = uint64_t{1} << 30;  // analysis safety limit

struct FileAnalysis {
	std::string name;
	std::string path;
	uint64_t file_bytes = 0;
	uint16_t format_version = 0;
	uint8_t index_width = 0;
	std::string sha256;
	ProgramStats program;
};

std::vector<uint8_t> read_bytes(const std::string& path) {
	std::ifstream in(path, std::ios::binary | std::ios::ate);
	expecting(in.is_open(), [&] { return "empbc-tool: cannot open " + path; });
	std::streampos end = in.tellg();
	expecting(end >= 0, [&] { return "empbc-tool: cannot size " + path; });
	const uint64_t size = static_cast<uint64_t>(end);
	expecting(size <= kMaxFileBytes, [&] {
		return "empbc-tool: file exceeds 1 GiB analysis limit: " + path;
	});
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	in.seekg(0);
	if (!bytes.empty()) in.read(reinterpret_cast<char *>(bytes.data()), bytes.size());
	expecting(in.good() || in.eof(), [&] { return "empbc-tool: read failed: " + path; });
	expecting(static_cast<uint64_t>(in.gcount()) == size || size == 0, [&] {
		return "empbc-tool: short read: " + path;
	});
	return bytes;
}

std::string read_text(const std::string& path) {
	std::vector<uint8_t> bytes = read_bytes(path);
	return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size());
}

std::string sha256_hex(const std::vector<uint8_t>& bytes) {
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digest_len = 0;
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();
	expecting(ctx != nullptr, "empbc-tool: EVP_MD_CTX_new");
	expecting(EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1
	          && EVP_DigestUpdate(ctx, bytes.data(), bytes.size()) == 1
	          && EVP_DigestFinal_ex(ctx, digest, &digest_len) == 1,
	          "empbc-tool: SHA-256 failed");
	EVP_MD_CTX_free(ctx);
	std::ostringstream out;
	out << std::hex << std::setfill('0');
	for (unsigned int i = 0; i < digest_len; ++i)
		out << std::setw(2) << static_cast<unsigned int>(digest[i]);
	return out.str();
}

std::string hex_u64(uint64_t value) {
	std::ostringstream out;
	out << "0x" << std::hex << std::setw(16) << std::setfill('0') << value;
	return out.str();
}

std::string json_escape(const std::string& value) {
	std::ostringstream out;
	for (unsigned char c : value) {
		switch (c) {
			case '"': out << "\\\""; break;
			case '\\': out << "\\\\"; break;
			case '\b': out << "\\b"; break;
			case '\f': out << "\\f"; break;
			case '\n': out << "\\n"; break;
			case '\r': out << "\\r"; break;
			case '\t': out << "\\t"; break;
			default:
				if (c < 0x20)
					out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
					    << static_cast<unsigned int>(c) << std::dec;
				else
					out << static_cast<char>(c);
		}
	}
	return out.str();
}

FileAnalysis inspect_file(const std::string& path, const std::string& name = {}) {
	std::vector<uint8_t> bytes = read_bytes(path);
	BooleanProgram program = load_empbc(bytes);
	FileAnalysis out;
	out.name = name.empty() ? fs::path(path).filename().string() : name;
	out.path = path;
	out.file_bytes = bytes.size();
	// load_empbc has already validated the fixed header.
	out.format_version = static_cast<uint16_t>(bytes[4]) |
	                     (static_cast<uint16_t>(bytes[5]) << 8);
	out.index_width = bytes[6];
	out.sha256 = sha256_hex(bytes);
	out.program = analyze_program(program);
	return out;
}

std::vector<std::string> empbc_files(const std::string& directory) {
	std::error_code ec;
	fs::directory_iterator it(directory, ec), end;
	expecting(!ec, [&] { return "empbc-tool: cannot list " + directory + ": " + ec.message(); });
	std::vector<std::string> files;
	while (it != end) {
		const bool regular = it->is_regular_file(ec);
		expecting(!ec, [&] { return "empbc-tool: cannot inspect directory entry: " + ec.message(); });
		if (regular && it->path().extension() == ".empbc")
			files.push_back(it->path().string());
		it.increment(ec);
		expecting(!ec, [&] { return "empbc-tool: directory iteration failed: " + ec.message(); });
	}
	std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) {
		return fs::path(a).filename().string() < fs::path(b).filename().string();
	});
	return files;
}

std::string analysis_json(const FileAnalysis& a, const std::string& indent = "  ") {
	const ProgramStats& s = a.program;
	std::ostringstream out;
	out << indent << "{\n"
	    << indent << "  \"name\": \"" << json_escape(a.name) << "\",\n"
	    << indent << "  \"sha256\": \"" << a.sha256 << "\",\n"
	    << indent << "  \"file_bytes\": " << a.file_bytes << ",\n"
	    << indent << "  \"format_version\": " << a.format_version << ",\n"
	    << indent << "  \"index_width\": " << static_cast<unsigned int>(a.index_width) << ",\n"
	    << indent << "  \"wire_reuse\": \"" << wire_reuse_name(s.wire_reuse) << "\",\n"
	    << indent << "  \"num_inputs\": " << s.num_inputs << ",\n"
	    << indent << "  \"num_outputs\": " << s.num_outputs << ",\n"
	    << indent << "  \"stored_num_wires\": " << s.stored_num_wires << ",\n"
	    << indent << "  \"dense_num_wires\": " << s.dense_num_wires << ",\n"
	    << indent << "  \"num_gates\": " << s.num_gates << ",\n"
	    << indent << "  \"num_and\": " << s.num_and << ",\n"
	    << indent << "  \"num_xor\": " << s.num_xor << ",\n"
	    << indent << "  \"num_not\": " << s.num_not << ",\n"
	    << indent << "  \"num_const\": " << s.num_const << ",\n"
	    << indent << "  \"and_depth\": " << s.and_depth << ",\n"
	    << indent << "  \"max_and_per_level\": " << s.max_and_per_level << ",\n"
	    << indent << "  \"reachable_wires\": " << s.reachable_wires << ",\n"
	    << indent << "  \"reachable_gates\": " << s.reachable_gates << ",\n"
	    << indent << "  \"reachable_and\": " << s.reachable_and << ",\n"
	    << indent << "  \"dead_gates\": " << (s.num_gates - s.reachable_gates) << ",\n"
	    << indent << "  \"peak_live_wires\": " << s.peak_live_wires << ",\n"
	    << indent << "  \"max_fanout\": " << s.max_fanout << ",\n"
	    << indent << "  \"stored_program_digest\": \"" << hex_u64(s.stored_program_digest) << "\",\n"
	    << indent << "  \"dense_program_digest\": \"" << hex_u64(s.dense_program_digest) << "\"\n"
	    << indent << "}";
	return out.str();
}

std::string manifest_entry_json(const FileAnalysis& a) {
	const ProgramStats& s = a.program;
	std::ostringstream out;
	out << "{\n"
	    << "  \"name\": \"" << json_escape(a.name) << "\",\n"
	    << "  \"sha256\": \"" << a.sha256 << "\",\n"
	    << "  \"num_inputs\": " << s.num_inputs << ",\n"
	    << "  \"num_outputs\": " << s.num_outputs << ",\n"
	    << "  \"num_gates\": " << s.num_gates << ",\n"
	    << "  \"num_and\": " << s.num_and << ",\n"
	    << "  \"and_depth\": " << s.and_depth << "\n"
	    << "}\n";
	return out.str();
}

std::string manifest_filename(const std::string& asset_path) {
	return fs::path(asset_path).stem().string() + ".json";
}

void write_manifest_set(const std::string& asset_directory,
						const std::string& manifest_directory) {
	std::vector<std::string> assets = empbc_files(asset_directory);
	std::error_code ec;
	fs::create_directories(manifest_directory, ec);
	expecting(!ec, [&] {
		return "empbc-tool: cannot create " + manifest_directory + ": " + ec.message();
	});

	std::vector<std::string> expected_names;
	for (const std::string& asset : assets) {
		const std::string filename = manifest_filename(asset);
		expected_names.push_back(filename);
		FileAnalysis a = inspect_file(asset, fs::path(asset).filename().string());
		const std::string contents = manifest_entry_json(a);
		const fs::path output = fs::path(manifest_directory) / filename;
		std::ofstream out(output, std::ios::binary);
		expecting(out.is_open(), [&] {
			return "empbc-tool: cannot write " + output.string();
		});
		out.write(contents.data(), contents.size());
		expecting(out.good(), [&] {
			return "empbc-tool: write failed: " + output.string();
		});
	}
	std::sort(expected_names.begin(), expected_names.end());

	// Regeneration also removes stale per-asset entries after an asset is
	// deleted or renamed. The output directory is dedicated to manifests.
	fs::directory_iterator it(manifest_directory, ec), end;
	expecting(!ec, [&] {
		return "empbc-tool: cannot list " + manifest_directory + ": " + ec.message();
	});
	while (it != end) {
		if (it->is_regular_file(ec) && it->path().extension() == ".json") {
			const std::string filename = it->path().filename().string();
			if (!std::binary_search(expected_names.begin(), expected_names.end(), filename)) {
				fs::remove(it->path(), ec);
				expecting(!ec, [&] {
					return "empbc-tool: cannot remove stale manifest " +
					       it->path().string() + ": " + ec.message();
				});
			}
		}
		expecting(!ec, [&] {
			return "empbc-tool: cannot inspect manifest entry: " + ec.message();
		});
		it.increment(ec);
		expecting(!ec, [&] {
			return "empbc-tool: manifest iteration failed: " + ec.message();
		});
	}
}

bool check_manifest_set(const std::string& manifest_directory,
						const std::string& asset_directory) {
	std::vector<std::string> assets = empbc_files(asset_directory);
	std::vector<std::string> expected_names;
	bool matches = true;
	for (const std::string& asset : assets) {
		const std::string filename = manifest_filename(asset);
		expected_names.push_back(filename);
		const fs::path manifest_path = fs::path(manifest_directory) / filename;
		std::error_code ec;
		if (!fs::is_regular_file(manifest_path, ec) || ec) {
			std::cerr << "missing manifest: " << manifest_path.string() << '\n';
			matches = false;
			continue;
		}
		FileAnalysis a = inspect_file(asset, fs::path(asset).filename().string());
		if (read_text(manifest_path.string()) != manifest_entry_json(a)) {
			std::cerr << "manifest mismatch: " << manifest_path.string() << '\n';
			matches = false;
		}
	}
	std::sort(expected_names.begin(), expected_names.end());

	std::error_code ec;
	fs::directory_iterator it(manifest_directory, ec), end;
	if (ec) {
		std::cerr << "cannot list manifest directory " << manifest_directory
		          << ": " << ec.message() << '\n';
		return false;
	}
	while (it != end) {
		const bool regular = it->is_regular_file(ec);
		if (ec) {
			std::cerr << "cannot inspect manifest entry: " << ec.message() << '\n';
			return false;
		}
		if (regular && it->path().extension() == ".json") {
			const std::string filename = it->path().filename().string();
			if (!std::binary_search(expected_names.begin(), expected_names.end(), filename)) {
				std::cerr << "unexpected manifest: " << it->path().string() << '\n';
				matches = false;
			}
		}
		it.increment(ec);
		if (ec) {
			std::cerr << "manifest iteration failed: " << ec.message() << '\n';
			return false;
		}
	}
	return matches;
}

void print_human(const FileAnalysis& a) {
	const ProgramStats& s = a.program;
	std::cout
		<< a.path << "\n"
		<< "  sha256              " << a.sha256 << "\n"
		<< "  format/index        v" << a.format_version << " / u"
		<< static_cast<unsigned int>(a.index_width * 8) << "\n"
		<< "  file bytes          " << a.file_bytes << "\n"
		<< "  wire reuse          " << wire_reuse_name(s.wire_reuse) << "\n"
		<< "  inputs/outputs      " << s.num_inputs << " / " << s.num_outputs << "\n"
		<< "  wires stored/dense  " << s.stored_num_wires << " / " << s.dense_num_wires << "\n"
		<< "  gates               " << s.num_gates << " (AND " << s.num_and
		<< ", XOR " << s.num_xor << ", NOT " << s.num_not
		<< ", CONST " << s.num_const << ")\n"
		<< "  AND depth/max level " << s.and_depth << " / " << s.max_and_per_level << "\n"
		<< "  reachable gates     " << s.reachable_gates << " (AND " << s.reachable_and << ")\n"
		<< "  dead gates          " << (s.num_gates - s.reachable_gates) << "\n"
		<< "  peak live/max fanout " << s.peak_live_wires << " / " << s.max_fanout << "\n"
		<< "  program digest      " << hex_u64(s.stored_program_digest) << "\n";
}

void print_usage() {
	std::cerr
		<< "usage:\n"
		<< "  empbc-tool inspect [--json] FILE...\n"
		<< "  empbc-tool compare [--json] OLD.empbc NEW.empbc\n"
		<< "  empbc-tool manifest --output MANIFEST_DIR ASSET_DIR\n"
		<< "  empbc-tool check-manifest MANIFEST_DIR ASSET_DIR\n";
}

int inspect_command(int argc, char **argv) {
	bool json = false;
	std::vector<std::string> paths;
	for (int i = 2; i < argc; ++i) {
		if (std::string(argv[i]) == "--json") json = true;
		else paths.emplace_back(argv[i]);
	}
	if (paths.empty()) return 2;
	std::vector<FileAnalysis> analyses;
	for (const std::string& path : paths) analyses.push_back(inspect_file(path));
	if (!json) {
		for (const auto& a : analyses) print_human(a);
		return 0;
	}
	if (analyses.size() == 1) {
		std::cout << analysis_json(analyses.front(), "") << '\n';
	} else {
		std::cout << "[\n";
		for (size_t i = 0; i < analyses.size(); ++i) {
			std::cout << analysis_json(analyses[i], "  ");
			if (i + 1 != analyses.size()) std::cout << ',';
			std::cout << '\n';
		}
		std::cout << "]\n";
	}
	return 0;
}

int compare_command(int argc, char **argv) {
	bool json = false;
	std::vector<std::string> paths;
	for (int i = 2; i < argc; ++i) {
		if (std::string(argv[i]) == "--json") json = true;
		else paths.emplace_back(argv[i]);
	}
	if (paths.size() != 2) return 2;
	FileAnalysis before = inspect_file(paths[0]);
	FileAnalysis after = inspect_file(paths[1]);
	if (json) {
		std::cout << "{\n  \"before\":\n" << analysis_json(before, "  ")
		          << ",\n  \"after\":\n" << analysis_json(after, "  ")
		          << "\n}\n";
		return 0;
	}
	auto delta = [](uint64_t a, uint64_t b) {
		return static_cast<int64_t>(b) - static_cast<int64_t>(a);
	};
	std::cout << "metric                 before          after          delta\n"
	          << "file bytes       " << std::setw(14) << before.file_bytes << std::setw(15)
	          << after.file_bytes << std::setw(15) << delta(before.file_bytes, after.file_bytes) << '\n'
	          << "stored wires     " << std::setw(14) << before.program.stored_num_wires << std::setw(15)
	          << after.program.stored_num_wires << std::setw(15)
	          << delta(before.program.stored_num_wires, after.program.stored_num_wires) << '\n'
	          << "gates            " << std::setw(14) << before.program.num_gates << std::setw(15)
	          << after.program.num_gates << std::setw(15)
	          << delta(before.program.num_gates, after.program.num_gates) << '\n'
	          << "AND gates        " << std::setw(14) << before.program.num_and << std::setw(15)
	          << after.program.num_and << std::setw(15)
	          << delta(before.program.num_and, after.program.num_and) << '\n'
	          << "AND depth        " << std::setw(14) << before.program.and_depth << std::setw(15)
	          << after.program.and_depth << std::setw(15)
	          << delta(before.program.and_depth, after.program.and_depth) << '\n'
	          << "peak live wires  " << std::setw(14) << before.program.peak_live_wires << std::setw(15)
	          << after.program.peak_live_wires << std::setw(15)
	          << delta(before.program.peak_live_wires, after.program.peak_live_wires) << '\n'
	          << "signature compatible: "
	          << ((before.program.num_inputs == after.program.num_inputs &&
	               before.program.num_outputs == after.program.num_outputs) ? "yes" : "no") << '\n';
	return 0;
}

int manifest_command(int argc, char **argv) {
	std::string output_directory;
	std::string directory;
	for (int i = 2; i < argc; ++i) {
		if (std::string(argv[i]) == "--output") {
			if (++i >= argc) return 2;
			output_directory = argv[i];
		} else if (directory.empty()) {
			directory = argv[i];
		} else {
			return 2;
		}
	}
	if (directory.empty() || output_directory.empty()) return 2;
	write_manifest_set(directory, output_directory);
	std::cout << "wrote manifests for " << empbc_files(directory).size()
	          << " assets to " << output_directory << '\n';
	return 0;
}

int check_manifest_command(int argc, char **argv) {
	if (argc != 4) return 2;
	if (check_manifest_set(argv[2], argv[3])) {
		std::cout << "manifests match " << argv[3] << '\n';
		return 0;
	}
	std::cerr << "regenerate with: empbc-tool manifest --output " << argv[2]
	          << ' ' << argv[3] << '\n';
	return 1;
}

}  // namespace

int main(int argc, char **argv) {
	if (argc < 2) {
		print_usage();
		return 2;
	}
	const std::string command = argv[1];
	int status = 2;
	if (command == "inspect") status = inspect_command(argc, argv);
	else if (command == "compare") status = compare_command(argc, argv);
	else if (command == "manifest") status = manifest_command(argc, argv);
	else if (command == "check-manifest") status = check_manifest_command(argc, argv);
	else if (command == "--help" || command == "help") {
		print_usage();
		return 0;
	}
	if (status == 2) print_usage();
	return status;
}
