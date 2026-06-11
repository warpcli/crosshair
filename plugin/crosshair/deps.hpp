#pragma once

#include <src/Compositor.hpp>
#include <src/event/EventBus.hpp>
#include <src/managers/CursorManager.hpp>
#include <src/managers/input/InputManager.hpp>
#include <src/managers/PointerManager.hpp>
#include <src/managers/eventLoop/EventLoopManager.hpp>
#include <src/managers/eventLoop/EventLoopTimer.hpp>
#include <src/plugins/PluginAPI.hpp>
#include <src/protocols/CursorShape.hpp>
#include <src/render/Renderer.hpp>
#include <src/render/pass/TexPassElement.hpp>

#include <cairo/cairo.h>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <optional>
#include <string>
#include <linux/input-event-codes.h>
