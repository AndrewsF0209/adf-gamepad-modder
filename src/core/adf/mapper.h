#pragma once

#include <functional>
#include <string>

#include "adf/config.h"

namespace adf {

// Runs the mapping loop until alive() returns false.
// foregroundPid: PID that must own the foreground window for input to be
// injected (the game process in both proxy-DLL and launcher deployments).
int RunMapper(const Config& cfg,
              unsigned long foregroundPid,
              const std::function<bool()>& alive,
              std::string& err);

// Convenience: load JSON then run.
int RunMapperFromFile(const std::wstring& jsonPath,
                      unsigned long foregroundPid,
                      const std::function<bool()>& alive,
                      std::string& err);

} // namespace adf
