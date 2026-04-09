#pragma once

#include <vector>
#include "default_init_allocator.h"

namespace Ship {
using Buffer = std::vector<char, default_init_allocator<char>>;
}
