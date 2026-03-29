#include "Core/Profiling.h"

#if defined(DS_ENABLE_TRACY)

#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
    constexpr int kTracyMemoryCallstackDepth = 8;

    void *AllocateTracked(std::size_t size)
    {
        const std::size_t allocationSize = (size == 0) ? 1 : size;
        void *ptr = std::malloc(allocationSize);
        if (ptr != nullptr)
        {
            TracyAllocS(ptr, allocationSize, kTracyMemoryCallstackDepth);
        }
        return ptr;
    }

    void FreeTracked(void *ptr) noexcept
    {
        if (ptr != nullptr)
        {
            TracyFreeS(ptr, kTracyMemoryCallstackDepth);
        }
        std::free(ptr);
    }

#if defined(_MSC_VER)
    void *AllocateAlignedTracked(std::size_t size, std::size_t alignment)
    {
        const std::size_t allocationSize = (size == 0) ? 1 : size;
        void *ptr = _aligned_malloc(allocationSize, alignment);
        if (ptr != nullptr)
        {
            TracyAllocS(ptr, allocationSize, kTracyMemoryCallstackDepth);
        }
        return ptr;
    }

    void FreeAlignedTracked(void *ptr) noexcept
    {
        if (ptr != nullptr)
        {
            TracyFreeS(ptr, kTracyMemoryCallstackDepth);
        }
        _aligned_free(ptr);
    }
#endif
} // namespace

void *operator new(std::size_t size)
{
    if (void *ptr = AllocateTracked(size))
    {
        return ptr;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t size)
{
    if (void *ptr = AllocateTracked(size))
    {
        return ptr;
    }
    throw std::bad_alloc();
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept
{
    return AllocateTracked(size);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept
{
    return AllocateTracked(size);
}

void operator delete(void *ptr) noexcept
{
    FreeTracked(ptr);
}

void operator delete[](void *ptr) noexcept
{
    FreeTracked(ptr);
}

void operator delete(void *ptr, std::size_t) noexcept
{
    FreeTracked(ptr);
}

void operator delete[](void *ptr, std::size_t) noexcept
{
    FreeTracked(ptr);
}

void operator delete(void *ptr, const std::nothrow_t &) noexcept
{
    FreeTracked(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t &) noexcept
{
    FreeTracked(ptr);
}

#if defined(_MSC_VER)
void *operator new(std::size_t size, std::align_val_t alignment)
{
    if (void *ptr = AllocateAlignedTracked(size, static_cast<std::size_t>(alignment)))
    {
        return ptr;
    }
    throw std::bad_alloc();
}

void *operator new[](std::size_t size, std::align_val_t alignment)
{
    if (void *ptr = AllocateAlignedTracked(size, static_cast<std::size_t>(alignment)))
    {
        return ptr;
    }
    throw std::bad_alloc();
}

void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return AllocateAlignedTracked(size, static_cast<std::size_t>(alignment));
}

void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept
{
    return AllocateAlignedTracked(size, static_cast<std::size_t>(alignment));
}

void operator delete(void *ptr, std::align_val_t) noexcept
{
    FreeAlignedTracked(ptr);
}

void operator delete[](void *ptr, std::align_val_t) noexcept
{
    FreeAlignedTracked(ptr);
}

void operator delete(void *ptr, std::size_t, std::align_val_t) noexcept
{
    FreeAlignedTracked(ptr);
}

void operator delete[](void *ptr, std::size_t, std::align_val_t) noexcept
{
    FreeAlignedTracked(ptr);
}

void operator delete(void *ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    FreeAlignedTracked(ptr);
}

void operator delete[](void *ptr, std::align_val_t, const std::nothrow_t &) noexcept
{
    FreeAlignedTracked(ptr);
}
#endif

#endif
