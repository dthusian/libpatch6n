#ifndef PATCH6N_PLATFORM_ID_HH
#define PATCH6N_PLATFORM_ID_HH

#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) || defined(__x86_64) || defined(_M_X64) || defined(_M_AMD64)
#define PLATFORM_ARCH_AMD64
#else
#error "Unsupported architecture"
#endif

#if defined(unix) || defined(__unix__) || defined(__unix)
#include <unistd.h>
#endif
#if defined(_POSIX_VERSION)
#define PLATFORM_OS_POSIX
#else
#error "Unsupported operating system"
#endif

#if defined(__GNUC__)
#define PLATFORM_CC_GCC
#else
#error "Unsupported compiler"
#endif

#endif //PATCH6N_PLATFORM_ID_HH
