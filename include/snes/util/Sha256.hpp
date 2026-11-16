#ifndef SNES_UTIL_SHA256_HPP
#define SNES_UTIL_SHA256_HPP

#include <cstddef>
#include <string>
#include <vector>
#include "snes/Types.hpp"

namespace snes {

class Sha256 {
public:
    Sha256();
    void update(const void* data, std::size_t size);
    std::string finalHex();

    static std::string digest(const void* data, std::size_t size);
    static std::string digest(const std::vector<u8>& data);
    static bool digestFile(const std::string& path, std::string& hex, std::string& error);

private:
    void transform(const u8 block[64]);
    u32 state_[8];
    u8 buffer_[64];
    std::size_t buffered_;
    u64 totalBytes_;
    bool finalized_;
};

} // namespace snes

#endif
