#include "files.hpp"

#include <filesystem>
#include <limits>
#include <random>

namespace qoiw::cli {
namespace {
namespace fs = std::filesystem;

class StagingFile {
   public:
    explicit StagingFile(const fs::path& parent) {
        std::random_device random;
        for (int attempt = 0; attempt < 16; ++attempt) {
            const auto name = ".qoiw-" + std::to_string(random()) + "-" + std::to_string(random());
            directory_ = parent / name;
            path_ = directory_ / "output";
            std::error_code error;
            if (fs::create_directory(directory_, error)) return;
            if (error && error != std::errc::file_exists)
                throw fs::filesystem_error("create output staging directory", directory_, error);
        }
        throw std::runtime_error("could not create an output staging directory");
    }

    ~StagingFile() {
        std::error_code ignored;
        fs::remove(path_, ignored);
        fs::remove(directory_, ignored);
    }

    StagingFile(const StagingFile&) = delete;
    StagingFile& operator=(const StagingFile&) = delete;
    const fs::path& path() const { return path_; }

   private:
    fs::path directory_;
    fs::path path_;
};
}  // namespace

InputFile::InputFile(const std::string& path) : path_(path) {
    if (!fs::is_regular_file(path)) throw std::runtime_error("input is missing or is not a regular file " + path);
    file_.open(path, std::ios::binary | std::ios::ate);
    if (!file_) throw std::runtime_error("cannot open " + path);
    const auto length = static_cast<std::streamoff>(file_.tellg());
    if (length < 0 || static_cast<std::uintmax_t>(length) > std::numeric_limits<std::size_t>::max() ||
        static_cast<std::uintmax_t>(length) > std::numeric_limits<std::streamsize>::max())
        throw std::runtime_error("cannot represent the byte length of " + path);
    size_ = static_cast<std::size_t>(length);
    file_.seekg(0);
    if (!file_) throw std::runtime_error("cannot seek to the start of " + path);
}

void InputFile::read(void* data) {
    if (size_ && !file_.read(static_cast<char*>(data), static_cast<std::streamsize>(size_)))
        throw std::runtime_error("cannot read " + path_);
    if (file_.peek() != std::char_traits<char>::eof() || file_.bad())
        throw std::runtime_error("input changed or could not be read in " + path_);
}

std::vector<std::uint8_t> read_container(const std::string& path) {
    InputFile file(path);
    std::vector<std::uint8_t> bytes(file.size());
    file.read(bytes.data());
    return bytes;
}

void write_file(const std::string& path, const void* data, std::size_t bytes) {
    if (bytes > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("cannot represent the output byte length of " + path);
    const fs::path destination(path);
    if (fs::exists(destination) && !fs::is_regular_file(destination))
        throw std::runtime_error("output must be a regular file " + path);
    const auto parent = destination.has_parent_path() ? destination.parent_path() : fs::path(".");
    const StagingFile staged(parent);
    std::ofstream output(staged.path(), std::ios::binary);
    if (!output) throw std::runtime_error("cannot open output for " + path);
    output.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    output.close();
    if (!output) throw std::runtime_error("cannot write " + path);
    fs::rename(staged.path(), destination);
}

void check_distinct_files(const std::string& input, const std::string& output) {
    if (!output.empty() && fs::exists(output) && fs::equivalent(input, output))
        throw std::invalid_argument("output path refers to the input file");
}

}  // namespace qoiw::cli
