#pragma once

namespace core {

// Engine-wide settings, independent of any renderer. Edited by the GUI and
// applied by whoever owns the affected system.
struct EngineSettings {
  bool isVsyncEnabled = true;
};

} // namespace core
