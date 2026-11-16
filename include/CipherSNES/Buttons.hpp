// Created by Jacob Hodgkins
#pragma once
namespace snes {
// Serial order used by the original SNES controller ports.
enum class SnesButton : unsigned { B, Y, Select, Start, Up, Down, Left, Right, A, X, L, R, Count };
constexpr unsigned buttonCount=static_cast<unsigned>(SnesButton::Count);
}
