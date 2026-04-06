// Stub implementations of platform-specific Vst3Plugin methods.
// Used by test binaries that don't link a platform-specific
// vst3_host_{x11,win32,mac} library.

#include "vst3_host.hpp"

#include <cstdint>
#include <vector>

namespace hibiki {

void Vst3Plugin::showEditor() {}
void Vst3Plugin::stopEditor() {}

bool Vst3Plugin::captureEditorFrame(std::vector<uint8_t>& /*rgba*/, int& /*w*/,
                                    int& /*h*/) {
  return false;
}

void Vst3Plugin::sendEditorInput(int /*type*/, int /*x*/, int /*y*/,
                                 int /*button*/, int /*key_code*/,
                                 int /*delta*/) {}

std::vector<std::string> Vst3Plugin::getDefaultVst3Dirs() { return {}; }

}  // namespace hibiki
