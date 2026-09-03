#include "tokens.h"
#include "file.h"
#include "ast.h"
#include "llvm_emitter.h"
#include "global.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace {

std::string normalized_path(const std::filesystem::path& path) {
	std::error_code error;
	std::filesystem::path absolute = std::filesystem::absolute(path, error);
	return (error ? path : absolute).lexically_normal().string();
}

std::filesystem::path resolve_import_path(
	const std::string& keyword,
	const std::string& requested_path,
	const std::string& importing_file
) {
	std::filesystem::path path = requested_path;
	if (path.is_absolute()) {
		return path;
	}

	
	
	
	if (keyword == "ftake") {
		return std::filesystem::path(importing_file).parent_path() / path;
	}
	if (!base_root.empty()) {
		return std::filesystem::path(base_root) / path;
	}
	return path;
}

void expand_imports(
	std::vector<Token>& tokens,
	const std::string& importing_file,
	std::set<std::string>& expanded_files
) {
	for (size_t i = 0; i < tokens.size();) {
		const bool is_take = tokens[i].value == "take";
		const bool is_ftake = tokens[i].value == "ftake";
		if ((!is_take && !is_ftake) || i + 1 >= tokens.size() ||
			tokens[i + 1].type != TSTRING) {
			++i;
			continue;
		}

		const std::string keyword = tokens[i].value;
		const std::string requested_path = tokens[i + 1].value;
		const std::string normalized = normalized_path(resolve_import_path(
			keyword, requested_path, importing_file));

		tokens.erase(tokens.begin() + i, tokens.begin() + i + 2);
		if (!expanded_files.insert(normalized).second) {
			continue;
		}

		if (!std::filesystem::is_regular_file(normalized)) {
			gerror(
				std::string("Cannot open ") +
				(is_ftake ? "ftaken" : "taken") +
				" file: " + normalized + "\n"
			);
			continue;
		}

		const std::string code = fileread(normalized);
		std::vector<Token> included_tokens = tokenize(code);
		expand_imports(included_tokens, normalized, expanded_files);
		if (!included_tokens.empty() && included_tokens.back().type == TCODEEND) {
			included_tokens.pop_back();
		}

		tokens.insert(
			tokens.begin() + i,
			included_tokens.begin(),
			included_tokens.end()
		);
		i += included_tokens.size();
	}
}

} // namespace

void process_takes(std::vector<Token>& tokens, const std::string& entry_file) {
	std::set<std::string> expanded_files;
	const std::string normalized_entry = normalized_path(entry_file);
	expanded_files.insert(normalized_entry);
	expand_imports(tokens, normalized_entry, expanded_files);
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "Usage: ferra file.fe -o output.ll\n";
		return 1;
	}

	std::string file_path;
	std::string output_path;

	for (int i = 1; i < argc; ++i) {
		std::string arg = argv[i];

		if (arg == "--llvm") {
			continue;
		} else if (arg == "-o") {
			if (i + 1 >= argc) {
				std::cerr << "Missing output path after -o\n";
				return 1;
			}
			output_path = argv[++i];
		} else if (file_path.empty()) {
			file_path = std::move(arg);
		} else {
			std::cerr << "Unexpected compiler argument: " << arg << "\n";
			std::cerr << "Program arguments belong after the compiled executable, "
			             "for example: ./program first second\n";
			return 1;
		}
	}

	if (file_path.empty()) {
		std::cerr << "Missing input file\n";
		std::cerr << "Usage: ferra file.fe -o out.ll\n";
		return 1;
	}
	if (std::filesystem::path(file_path).extension() != ".fe") {
		std::cerr << "Only .fe source files are supported\n";
		return 1;
	}

	base_root = get_ferra_path(argv[0]);
	if (base_root.empty()) {
		std::filesystem::path candidate =
			std::filesystem::absolute(file_path).parent_path().lexically_normal();
		while (!candidate.empty()) {
			if (std::filesystem::is_directory(candidate / "fe")) {
				base_root = candidate.string();
				break;
			}
			const std::filesystem::path parent = candidate.parent_path();
			if (parent == candidate) {
				break;
			}
			candidate = parent;
		}
		if (base_root.empty()) {
			base_root = std::filesystem::absolute(file_path).parent_path().string();
		}
	}

	std::ifstream file(file_path);
	if (!file.is_open()) {
		std::cerr << "Cannot open: " << file_path << "\n";
		return 1;
	}
	const std::string code(
		(std::istreambuf_iterator<char>(file)),
		std::istreambuf_iterator<char>()
	);

	std::vector<Token> tokens = tokenize(code);
	g_had_compilation_error = false;
	process_takes(tokens, file_path);
	if (g_had_compilation_error) {
		return 1;
	}

	auto program = parse_ast(tokens);
	if (g_had_compilation_error) {
		return 1;
	}

	const std::string ir = generate_llvm_ir(*program);
	if (g_had_compilation_error) {
		return 1;
	}

	std::string final_output_path = output_path;
	if (final_output_path.empty()) {
		final_output_path = std::filesystem::path(file_path).replace_extension(".ll").string();
	}

	std::ofstream output(final_output_path);
	if (!output.is_open()) {
		std::cerr << "Cannot write: " << final_output_path << "\n";
		return 1;
	}
	output << ir;
	if (!output.good()) {
		std::cerr << "Cannot write: " << final_output_path << "\n";
		return 1;
	}

	std::cout << "Out: " << final_output_path << "\n";
	return 0;
}
