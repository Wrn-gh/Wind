#pragma once

#include <windows.h>

#include <array>

#include "wind/core/Selection.h"
#include "wind/ui/Theme.h"

namespace wind::ui {

class IconSet;

/// @brief 纯绘制组件：把选取状态画到叠加层上，并把用户操作翻译成命令返回。
///
/// 它不直接调用 SetWindowPos / SetLayeredWindowAttributes，属性修改一律通过返回值
/// 交回应用层执行，保证「界面层只读状态、只发命令」的分层约定。
///
/// 视觉参考输入法候选词条：一条深色圆角横条，所有操作等宽平铺在同一行，
/// 区块之间只用留白分隔、没有描边按钮框，悬停或按下时才浮出底色。
class ToolbarView {
public:
    /// @brief 用户在界面上触发的意图。
    struct Command {
        enum class Type { None, AdjustOpacity, SetOpacity, ToggleTopmost, Reset };

        Type type = Type::None;
        /// @brief AdjustOpacity / SetOpacity 使用的百分点。
        int value = 0;
    };

    /// @brief 绘制一帧。
    /// @param selection 当前选取状态（只读）。
    /// @param origin 叠加层客户区左上角对应的屏幕坐标（多显示器时可能为负），用于坐标换算。
    /// @return 用户本帧触发的命令；无操作时 type == None。
    Command Draw(const core::Selection& selection, POINT origin);

    /// @brief 设置界面缩放比例（与渲染器的 DPI 缩放保持一致）。
    void SetUiScale(float scale);

    /// @brief 指定工具栏图标来源；传 nullptr 则退回纯文字标签。
    ///
    /// 只持有指针不持有所有权：IconSet 的生命周期由应用层管理，且必须覆盖整个绘制周期。
    void SetIcons(const IconSet* icons);

    /// @brief 设置配色：明暗底表 + 可选自定义强调色，由应用层按设置算好后传入。
    void SetTheme(const Theme& theme) { theme_ = theme; }

    /// @brief 指针（屏幕坐标）是否停在工具栏上。
    ///
    /// 工具栏通常浮在目标窗口外侧，指针移过去时命中测试会落到它下面的窗口上；
    /// 应用层用这个判断来保持当前目标不变，否则工具栏会在鼠标移过去时消失。
    bool ContainsScreenPoint(POINT screenPoint) const;

    /// @brief 指针是否处在“工具栏保护区”内。
    ///
    /// 保护区 = 工具栏 + 工具栏与目标窗口之间的走廊。鼠标从目标窗口直线移向工具栏时，
    /// 中途会压在目标窗口之外的其它窗口上；这段时间同样必须冻结目标，
    /// 否则目标被换掉、工具栏跟着跳走，看起来就是“鼠标一移出去工具栏就没了”。
    bool ProtectsPoint(POINT screenPoint) const;

private:
    /// @brief 横条上的一个可点击区块。
    struct Segment {
        enum class Kind { Pin, Decrease, Opacity, Increase, Reset };

        Kind kind = Kind::Pin;
        /// @brief 相对横条左上角的偏移与宽度（物理像素）。
        float x = 0.0f;
        float width = 0.0f;
    };

    /// @brief 按当前缩放计算各区块位置与横条整体尺寸。
    void Layout();
    /// @brief 图标是否可用（可用时区块收窄成图标格，否则保留文字宽度）。
    bool IconsReady() const;
    /// @brief 把横条内的局部横坐标换算成命中的区块下标；未命中返回 -1。
    int HitSegment(float localX) const;
    void DrawBar(const core::Selection& selection, bool locked, float left, float top, int hoverIndex,
                 int heldIndex);
    void Trigger(const Segment& segment, bool locked, Command& command);
    void UpdateBarrier(const RECT& targetScreen, const RECT& barScreen);
    void DrawTargetFrame(const core::Selection& selection, POINT origin);
    void DrawHint(POINT origin);
    void SyncSlider(const core::Selection& selection);

    /// @brief 把 96 DPI 下的设计尺寸换算成当前 DPI 的物理像素。
    float Px(float value) const { return value * uiScale_; }

    int sliderValue_ = 100;
    /// 横条尺寸（物理像素），由 Layout() 计算，用于定位与命中测试。
    float barWidth_ = 0.0f;
    float barHeight_ = 0.0f;
    std::array<Segment, 5> segments_{};
    /// 鼠标按下时命中的区块下标，松开时用来判断是否仍然是同一块（-1 表示没有）。
    int pressedSegment_ = -1;
    /// 工具栏在屏幕坐标下的矩形，供 ContainsScreenPoint 使用。
    RECT screenBounds_{};
    /// 工具栏 + 通往目标窗口的走廊（屏幕坐标），供 ProtectsPoint 使用。
    RECT barrier_{};
    /// 图标纹理来源，可为空。
    const IconSet* icons_ = nullptr;
    /// 当前配色，默认深色；应用层按设置覆盖。
    Theme theme_ = kDarkTheme;
    float uiScale_ = 1.0f;
};

}  // namespace wind::ui
