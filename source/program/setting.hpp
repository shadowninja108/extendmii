#pragma once

#include "common.hpp"

#define EXL_MODULE_NAME "extendmii"
#define EXL_MODULE_NAME_LEN (sizeof(EXL_MODULE_NAME)-1)

#define EXL_DEBUG
#define EXL_USE_FAKEHEAP

/*
#define EXL_SUPPORTS_REBOOTPAYLOAD
*/

namespace exl::setting {
    /* How large the fake .bss heap will be. */
    constexpr size_t HeapSize = 0xA00000;

    /* How large the JIT area will be for hooks. */
    constexpr size_t JitSize = 0x1000;

    /* Sanity checks. */
    static_assert(ALIGN_UP(JitSize, PAGE_SIZE) == JitSize, "");
}