#include <EASTL/allocator.h>
#include <cstdio>
#include <cstdlib>
#include <new>

// 1. Исправляем Vsnprintf: объявляем внутри пространства eastl, чтобы компилятор не ругался
namespace eastl {
int Vsnprintf(char* pDestination, size_t n, const char* pFormat, va_list arguments) {
    return vsnprintf(pDestination, n, pFormat, arguments);
}
}

// 2. Глобальные операторы оставляем — они нужны для контейнеров, требующих кастомных выравниваний
void* operator new[](size_t size, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    return std::malloc(size);
}

void* operator new[](size_t size, size_t alignment, size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    void* p = nullptr;
    if (posix_memalign(&p, alignment, size) != 0) p = nullptr;
    return p;
#endif
}

void operator delete[](void* p, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
    std::free(p);
}

void operator delete[](void* p, size_t /*alignment*/, size_t /*alignmentOffset*/, const char* /*pName*/, int /*flags*/, unsigned /*debugFlags*/, const char* /*file*/, int /*line*/) {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}