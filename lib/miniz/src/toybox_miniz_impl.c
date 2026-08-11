/* Compiles the vendored miniz with Toybox's configuration. The include order
 * is load-bearing: the config defines/renames must be seen first. */
/* clang-format off */
#include "toybox_miniz.h"

#include "../third_party/miniz.c"
/* clang-format on */
