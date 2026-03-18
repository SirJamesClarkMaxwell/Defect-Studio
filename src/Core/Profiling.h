#pragma once

#if defined(DS_ENABLE_TRACY)
#include <tracy/Tracy.hpp>
#define DS_PROFILE_FRAME() FrameMark
#define DS_PROFILE_SCOPE() ZoneScoped
#define DS_PROFILE_SCOPE_N(name) ZoneScopedN(name)
#define DS_PROFILE_TEXT(text, size) TracyMessage(text, size)
#else
#define DS_PROFILE_FRAME() ((void)0)
#define DS_PROFILE_SCOPE() ((void)0)
#define DS_PROFILE_SCOPE_N(name) ((void)0)
#define DS_PROFILE_TEXT(text, size) ((void)0)
#endif
