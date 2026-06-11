#pragma once

/// @file threadmaxx_audio.hpp
/// @brief Umbrella include — pulls in every public AU1 header so consumers
/// can `#include <threadmaxx_audio/threadmaxx_audio.hpp>` and use the whole
/// surface in scope. Later batches (AU2-AU8) extend this header in lockstep
/// with their public additions.

#include "threadmaxx_audio/buffer.hpp"
#include "threadmaxx_audio/clip.hpp"
#include "threadmaxx_audio/config.hpp"
#include "threadmaxx_audio/device.hpp"
#include "threadmaxx_audio/diagnostics.hpp"
#include "threadmaxx_audio/dsp.hpp"
#include "threadmaxx_audio/events.hpp"
#include "threadmaxx_audio/loopback_device.hpp"
#include "threadmaxx_audio/mixer.hpp"
#include "threadmaxx_audio/scene.hpp"
#include "threadmaxx_audio/spatial.hpp"
#include "threadmaxx_audio/stream.hpp"
#include "threadmaxx_audio/types.hpp"
#include "threadmaxx_audio/version.hpp"
#include "threadmaxx_audio/voice.hpp"
