#include <Core/System/Implementation/xcb/InputDevice_xcb.h>

// clang-format off
EZ_BEGIN_DYNAMIC_REFLECTED_TYPE(ezStandardInputDevice, 1, ezRTTINoAllocator)
EZ_END_DYNAMIC_REFLECTED_TYPE;
// clang-format on

ezStandardInputDevice::ezStandardInputDevice(ezUInt32 uiWindowNumber) {}
ezStandardInputDevice::~ezStandardInputDevice() = default;

void ezStandardInputDevice::SetShowMouseCursor(bool bShow) {}

void ezStandardInputDevice::SetClipMouseCursor(ezMouseCursorClipMode::Enum mode) {}

ezMouseCursorClipMode::Enum ezStandardInputDevice::GetClipMouseCursor() const
{
  return ezMouseCursorClipMode::Default;
}

void ezStandardInputDevice::InitializeDevice() {}

void ezStandardInputDevice::RegisterInputSlots() {}