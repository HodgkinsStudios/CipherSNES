#ifndef SNES_CORE_SAVESTATE_HPP
#define SNES_CORE_SAVESTATE_HPP

#include <iosfwd>
#include <string>
#include <vector>
#include "snes/Types.hpp"

namespace snes {

class System;

class SaveStateManager {
public:
    SaveStateManager();
    bool save(const System& system, const std::string& path, std::string& error) const;
    bool load(System& system, const std::string& path, std::string& error) const;
    bool saveToMemory(const System& system, std::vector<u8>& output, std::string& error) const;
    bool loadFromMemory(System& system, const std::vector<u8>& input, std::string& error) const;
    // Trusted process-local snapshots for rewind. These retain the full state header and ROM
    // identity checks but deliberately omit the disk-style checksum trailer.
    bool saveTransientToMemory(const System& system, std::vector<u8>& output, std::string& error) const;
    bool loadTransientFromMemory(System& system, const std::vector<u8>& input, std::string& error) const;

private:
    bool savePayload(const System& system, std::ostream& output, std::string& error) const;
    bool loadPayload(System& system, std::istream& input, std::streamoff payloadSize,
                     std::string& error) const;
};

} // namespace snes

#endif
