#pragma once

#if defined(DS_ENABLE_TRACY)
#include <tracy/Tracy.hpp>
#define DS_PROFILE_FRAME() FrameMark
#define DS_PROFILE_SCOPE() ZoneScoped
#define DS_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define DS_PROFILE_TEXT(text, size) TracyMessage(text, size)
#define DS_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define DS_PROFILE_FREE(ptr) TracyFree(ptr)
#define DS_PROFILE_ALLOC_N(ptr, size, name) TracyAllocN(ptr, size, name)
#define DS_PROFILE_FREE_N(ptr, name) TracyFreeN(ptr, name)
#define DS_PROFILE_PLOT(name, value) TracyPlot(name, value)
#else
#define DS_PROFILE_FRAME() ((void)0)
#define DS_PROFILE_SCOPE() ((void)0)
#define DS_PROFILE_SCOPE_N(name) ((void)0)
#define DS_PROFILE_TEXT(text, size) ((void)0)
#define DS_PROFILE_ALLOC(ptr, size) ((void)0)
#define DS_PROFILE_FREE(ptr) ((void)0)
#define DS_PROFILE_ALLOC_N(ptr, size, name) ((void)0)
#define DS_PROFILE_FREE_N(ptr, name) ((void)0)
#define DS_PROFILE_PLOT(name, value) ((void)0)
#endif
