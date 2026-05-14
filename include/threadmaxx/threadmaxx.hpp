#pragma once

/// @file threadmaxx.hpp
/// Umbrella header — pulls in the full public API. Game code can simply do
/// @code
///     #include <threadmaxx/threadmaxx.hpp>
/// @endcode
/// or include the individual headers it actually uses.

#include "CommandBuffer.hpp"
#include "Components.hpp"
#include "Config.hpp"
#include "Engine.hpp"
#include "EventChannel.hpp"
#include "Game.hpp"
#include "Handles.hpp"
#include "Logger.hpp"
#include "Query.hpp"
#include "RenderFrame.hpp"
#include "Renderer.hpp"
#include "Resource.hpp"
#include "ScratchArena.hpp"
#include "Serialization.hpp"
#include "SkipPolicy.hpp"
#include "SpatialHash.hpp"
#include "Stats.hpp"
#include "System.hpp"
#include "TaskTag.hpp"
#include "Trace.hpp"
#include "UserComponent.hpp"
#include "World.hpp"

#include "render/Camera.hpp"
#include "render/DebugGeometry.hpp"
#include "render/DrawItem.hpp"
#include "render/InstanceBufferLayout.hpp"
#include "render/Light.hpp"
#include "render/RenderFrameBuilder.hpp"
#include "render/RenderPasses.hpp"
#include "render/UploadRing.hpp"
#include "render/Visibility.hpp"
