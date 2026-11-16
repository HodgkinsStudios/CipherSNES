#ifndef SNES_TYPES_HPP
#define SNES_TYPES_HPP

#include <cstddef>
#include <cstdint>

namespace snes {

typedef std::uint8_t  u8;
typedef std::uint16_t u16;
typedef std::uint32_t u32;
typedef std::uint64_t u64;
typedef std::int8_t   s8;
typedef std::int16_t  s16;
typedef std::int32_t  s32;
typedef std::int64_t  s64;

static const std::size_t KiB = 1024u;
static const std::size_t MiB = 1024u * KiB;

} // namespace snes

#endif
