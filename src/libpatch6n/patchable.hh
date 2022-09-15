
#ifndef PATCH6N_PATCHABLE_HH
#define PATCH6N_PATCHABLE_HH

#include "platform_id.hh"

#if defined(PLATFORM_CC_GCC) && defined(PLATFORM_ARCH_AMD64)
#define PATCHABLE __asm__ (".byte 102, 15, 31, 68, 0, 0, 102, 15, 31, 68, 0, 0")
#else
#error "Unsupported compiler+architecture combo"
#endif

#endif //PATCH6N_PATCHABLE_HH
