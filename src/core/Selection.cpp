#include "wind/core/Selection.h"

#include <algorithm>

#include "wind/platform/Win32Error.h"

namespace wind::core {

Selection::Selection(HWND overlayWindow, WindowRegistry& registry, const IgnoreList* ignore)
    : overlay_(overlayWindow), registry_(&registry), ignore_(ignore) {}

bool Selection::UpdatePointer(POINT screenPoint) {
    const TargetWindow found = TargetWindow::FindAt(screenPoint, overlay_, ignore_);
    const HWND currentHwnd = target_ != nullptr ? target_->Hwnd() : nullptr;

    if (found.Hwnd() == currentHwnd) {
        return false;
    }

    if (found.Hwnd() == nullptr) {
        // 指针落在桌面、工具栏或叠加层自身的空白处：保留上一次的目标。
        // 工具栏浮在目标窗口外侧，一旦按“指针下没有窗口”就清空目标，
        // 鼠标刚移向工具栏工具栏就消失了，用户根本没机会点它。
        registry_->DropDeadWindows();
        return false;
    }

    target_ = std::make_unique<TargetWindow>(found);
    // 同一窗口离开再回来时保留已调节的值，避免“回来就被重置”。
    registry_->Acquire(found.Hwnd());
    registry_->DropDeadWindows();
    return true;
}

WindowProperties* Selection::Current() {
    if (!HasTarget()) {
        return nullptr;
    }
    return &registry_->Acquire(target_->Hwnd());
}

const WindowProperties* Selection::Lookup() const {
    if (!HasTarget()) {
        return nullptr;
    }
    return registry_->Find(target_->Hwnd());
}

RECT Selection::TargetBounds() const {
    return HasTarget() ? target_->Bounds() : RECT{};
}

std::wstring Selection::TargetTitle() const {
    return HasTarget() ? target_->Title() : std::wstring{};
}

DWORD Selection::TargetProcessId() const {
    return HasTarget() ? target_->ProcessId() : 0;
}

int Selection::Opacity() const {
    const WindowProperties* properties = Lookup();
    return properties != nullptr ? properties->Opacity() : 100;
}

bool Selection::AlwaysOnTop() const {
    const WindowProperties* properties = Lookup();
    return properties != nullptr && properties->AlwaysOnTop();
}

bool Selection::OpacityLocked() const {
    const WindowProperties* properties = Lookup();
    return properties != nullptr && properties->OpacityLocked();
}

void Selection::AdjustOpacity(int delta) {
    if (WindowProperties* properties = Current(); properties != nullptr) {
        properties->SetOpacity(properties->Opacity() + delta);
    }
}

void Selection::SetOpacity(int percent) {
    if (WindowProperties* properties = Current(); properties != nullptr) {
        properties->SetOpacity(percent);
    }
}

void Selection::ToggleAlwaysOnTop() {
    if (WindowProperties* properties = Current(); properties != nullptr) {
        properties->SetAlwaysOnTop(!properties->AlwaysOnTop());
    }
}

void Selection::Reset() {
    if (WindowProperties* properties = Current(); properties != nullptr) {
        properties->Restore();
    }
}

}  // namespace wind::core
