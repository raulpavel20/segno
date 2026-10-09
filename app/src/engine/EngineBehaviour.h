#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace fd::engine
{
/// How tracktion behaves in Segno. Every tracktion::Engine is made with it (the app and the
/// tests), so mixing works the same everywhere:
/// - a track that isn't heard (muted, or silenced by another's solo) sends nothing to the
///   return tracks, even when it keeps running to feed a sidechain (ADR 0025, ADR 0026).
[[nodiscard]] std::unique_ptr<tracktion::EngineBehaviour> makeEngineBehaviour();
} // namespace fd::engine
