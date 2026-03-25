// Isolated TU for stb_image_write implementation.
// Excluded from clang-tidy to avoid false-positive diagnostics inside the header.
// NOLINTBEGIN
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
// NOLINTEND
