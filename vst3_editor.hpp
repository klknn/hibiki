#pragma once

#include <atomic>
#include <memory>
#include "pluginterfaces/vst/vsttypes.h"
#include "pluginterfaces/gui/iplugview.h"

namespace Steinberg {
namespace Vst {
class IEditController;
}
}

class IVst3Editor {
public:
    virtual ~IVst3Editor() {}
    virtual bool open(Steinberg::Vst::IEditController* controller, std::atomic<bool>& running, std::atomic<uint64_t>& window) = 0;
};

std::unique_ptr<IVst3Editor> createVst3Editor();
