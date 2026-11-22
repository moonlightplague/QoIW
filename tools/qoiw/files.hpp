#pragma once

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "qoiw/types.hpp"

namespace qoiw::cli {

class InputFile {
   public:
    explicit InputFile(const std::string& path);
    std::size_t size() const { return size_; }
    void read(void* data);

   private:
    std::string path_;
    std::ifstream file_;
    std::size_t size_;
};

template <class T>
void convert_byte_order(std::vector<T>& values) {
    const std::uint16_t one = 1;
    if (*reinterpret_cast<const unsigned char*>(&one) == 1) return;
    for (auto& value : values) {
        auto* bytes = reinterpret_cast<unsigned char*>(&value);
        std::reverse(bytes, bytes + sizeof(T));
    }
}

template <class T>
std::vector<T> read_raw(const std::string& path, std::size_t bytes) {
    InputFile file(path);
    if (file.size() != bytes) throw std::runtime_error("raw byte length disagrees with the shape in " + path);
    std::vector<T> values(bytes / sizeof(T));
    file.read(values.data());
    convert_byte_order(values);
    return values;
}

std::vector<std::uint8_t> read_container(const std::string& path);

// Publish only after the complete file has been written successfully.
void write_file(const std::string& path, const void* data, std::size_t bytes);
void check_distinct_files(const std::string& input, const std::string& output);

}  // namespace qoiw::cli
