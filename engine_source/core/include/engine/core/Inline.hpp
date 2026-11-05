#pragma once

#if defined(_MSC_VER)
    // MSVC: __forceinline уже подразумевает inline
    #define TRY_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
    // GCC/Clang: атрибут always_inline требует наличия inline
    #define TRY_INLINE inline __attribute__((always_inline))
#else
    // Прочие компиляторы — обычный inline
    #define TRY_INLINE inline
#endif