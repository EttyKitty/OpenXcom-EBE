#pragma once
// Precompiled header for fast Debug builds (ENABLE_PCH=ON)
// Keep list short: only headers included in >50% of TUs.
// Changing this file forces rebuild of all PCH users, so avoid project headers that change often.

// STL - most TUs include these
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// SDL - included in almost every Engine/Interface file
#include <SDL.h>
#include <SDL_mixer.h>
#include <SDL_image.h>

// YAML - heavy rapidyaml pull via Engine/Yaml.h (big win to precompile)
// Note: keep after STL/SDL to ensure macros not polluted
#include "Engine/Yaml.h"

// Frequently used project headers (stable, not churned daily)
// Avoid Options.h - it churns often and would invalidate PCH on every option tweak
#include "Engine/CrossPlatform.h"
#include "Engine/Logger.h"
#include "Engine/Exception.h"
