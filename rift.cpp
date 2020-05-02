#include <regex>
#include <optional>
#include "CLI11.hpp"

namespace fs = std::filesystem;

namespace rift {
	namespace file_tools {

		auto get_file_contents(const std::filesystem::path& path
		) -> std::optional<std::string> {
			std::ifstream filestream(path);
			if (!filestream.good()) {
				std::cerr << "Couldn't open " << path.string() << " for reading." << std::endl;
				return std::nullopt;
			}
			std::stringstream buffer;
			buffer << filestream.rdbuf();
			filestream.close();
			return buffer.str();
		}


		auto create_dir_if_necessary(const fs::path& dir) -> void {
			if (fs::exists(dir))
				return;

			// Recursively make sure the dirs parent also exists, otherwise fs::create_directory() fails
			const fs::path parent = dir.parent_path();
			if (dir == parent)
				throw std::runtime_error("Something wrong with the path");
			create_dir_if_necessary(dir.parent_path());
			fs::create_directory(dir);
		}


		auto create_dirs_for_file(const fs::path& path) -> void {
			// This function is called for files. create_dirs_for_path() operates on directories.
			// Fun fact: std::filesystem::is_directory() doesn't work on non-existing files
			create_dir_if_necessary(path.parent_path());
		}


		auto write_file(
			const fs::path& output_file,
			const std::string& contents
		) -> void {
			create_dirs_for_file(output_file);

			std::ofstream filestream(output_file);
			if (!filestream.good()) {
				std::cerr << "Couldn't open " << output_file.string() << " for writing." << std::endl;
				return;
			}
			filestream << contents;
			filestream.close();
		}


		auto write_file(
			const std::string& output_dir_str,
			const fs::path& input_file,
			const std::string& contents
		) -> void {
			fs::path output_file = fs::current_path().parent_path() / output_dir_str / input_file;
			write_file(output_file, contents);
		}


		auto get_all_file_contents()
			-> std::map<fs::path, std::string>
		{
			std::map<fs::path, std::string> contents;
			for (const auto& dir_entry : fs::recursive_directory_iterator(fs::current_path())) {
				if (!dir_entry.is_regular_file())
					continue;
				const fs::path relative_path = fs::relative(dir_entry.path());
				const std::optional<std::string> file_contents = file_tools::get_file_contents(dir_entry);
				if (!file_contents.has_value())
					continue;
				contents.emplace(relative_path, file_contents.value());
			}
			return contents;
		}

	} // namespace file_tools


	struct IncludeResult {
		std::string new_content;
		bool did_inclusion = false;
	};


	/// <summary>Replaces all inclusions in content with their content. This is not recursive on this level.</summary>
	auto include_run(
		const std::string& content,
		const std::map<fs::path, std::string>& all_contents,
		const std::string& regex_str
	) -> IncludeResult {
		std::regex rx(regex_str);
		auto words_begin = std::sregex_iterator(content.begin(), content.end(), rx);
		auto words_end = std::sregex_iterator();

		IncludeResult result;
		std::string rest = content;
		for (std::sregex_iterator it = words_begin; it != words_end; ++it) {
			result.new_content += it->prefix();
			if (it->size() < 2) {
				std::cerr << "regex doesn't include capture group" << std::endl;
				return IncludeResult{ content, false };
			}
			const std::string replace_path = (*it)[1];
			if (!all_contents.contains(replace_path)) {
				std::cerr << "included file \"" << replace_path << "\" doesn't exist -> ignoring" << std::endl;
				rest = it->suffix();
				continue;
			}
			result.new_content += all_contents.at(replace_path);
			result.did_inclusion = true;
			rest = it->suffix();
		}
		result.new_content += rest;
		return result;
	}


	/// <summary>Resursively replace the inclusions in the file with their content.</summary>
	auto recursive_include(
		const fs::path& path,
		const std::map<fs::path, std::string>& all_contents,
		const int max_inclusion_depth,
		const std::string& regex_str
	) -> std::string {
		std::string new_content = all_contents.at(path);
		for(int i=0; i < max_inclusion_depth; ++i){
			const IncludeResult result = include_run(new_content, all_contents, regex_str);
			if (!result.did_inclusion)
				return new_content;
			new_content = result.new_content;
		}
		std::cout << "max inclusion depth reached for " << path.string() << std::endl;
		return new_content;
	}


	// iteration limit is not properly implemented
	auto rift(
		const std::string& output_dir_str, 
		const int max_inclusion_depth,
		const std::string& regex_str
	) -> void {
		// Read all text files in the working dir
		const auto input_contents = file_tools::get_all_file_contents();

		// Resolve their inclusions
		std::map<fs::path, std::string> output_contents;
		for (const auto& [path, content] : input_contents) {
			output_contents.emplace(
				path,
				recursive_include(path, input_contents, max_inclusion_depth, regex_str)
			);
		}

		// Write them into out dir
		for (const auto& [input_file, content] : output_contents)
			file_tools::write_file(output_dir_str, input_file, content);
	}

} // namespace rift


auto main(const int argc, char** argv) -> int {
	CLI::App app{ "Recursively Include Text Files RIFT" };

	std::string output_dir;
	std::string regex_str = R"(#include \"([\w.\/%]*)\")";
	int max_inclusion_depth = 5;
	app.add_option("-o,--out_path", output_dir, "output directory")->required();
	app.add_option("-r,--regex", regex_str, "regex string");
	app.add_option("-d,--max_depth", max_inclusion_depth, "max inclusion depth");
	CLI11_PARSE(app, argc, argv);

	rift::rift(output_dir, max_inclusion_depth, regex_str);
	
	return 0;
}
