#pragma once

#include <windows.h>

#include <d3d11.h>
#include <dcomp.h>
#include <dxgi1_3.h>
#include <wrl/client.h>

#include <cstdint>

struct ImGuiContext;

namespace wind::platform {

/// @brief Direct3D 11 + DirectComposition + Dear ImGui 的组合渲染器。
///
/// 之所以用 CreateSwapChainForComposition 而不是普通的 CreateSwapChainForHwnd：
/// 只有合成交换链（DXGI_ALPHA_MODE_PREMULTIPLIED）才能让窗口内容逐像素半透明，
/// 从而做出“覆盖全屏但只有描边和工具栏可见”的叠加层。
///
/// 生命周期：Initialize -> (BeginFrame / EndFrame)* -> Shutdown。
/// Resize 可在任意时刻调用，内部会重建交换链（合成交换链的尺寸不可变）。
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    /// @brief 创建设备、交换链、合成视觉与 ImGui 上下文。
    /// @param targetWindow 叠加层窗口句柄（需已创建，不要求可见）。
    /// @param width 交换链宽度（物理像素）。
    /// @param height 交换链高度（物理像素）。
    /// @return 成功返回 true；失败时记录具体 HRESULT，调用方应放弃启动。
    bool Initialize(HWND targetWindow, int width, int height);

    /// @brief 释放全部资源；重复调用安全。
    void Shutdown();

    /// @brief 叠加层尺寸变化（显示器增删、分辨率变化）时重建交换链。
    bool Resize(int width, int height);

    /// @brief 设置界面缩放比例（DPI 缩放 × 基础尺寸），需在 Initialize 之后调用。
    /// @param scale 1.0 表示 96 DPI；界面字号、间距、控件尺寸随之整体放大。
    void SetUiScale(float scale);

    /// @brief 设置 ImGui 默认配色：明暗之一，并可叠加自定义强调色。
    /// @param light 是否使用亮色。
    /// @param accentRgb 强调色 0xRRGGBB；传 0 表示沿用 ImGui 默认强调色。
    ///
    /// 工具栏与描边都是手绘几何、自带配色，这里只影响标准控件
    /// （设置页的按钮、滑块、取色器等），但保持两边一致可以避免观感割裂。
    void SetTheme(bool light, uint32_t accentRgb);

    /// @brief 把本实例的 ImGui 上下文设为当前。
    ///
    /// 进程里同时存在叠加层与设置页两个渲染器、两个 ImGui 上下文。ImGui 的
    /// 绝大多数 API（含 ImGui_ImplWin32_WndProcHandler）都作用于“当前上下文”，
    /// 所以分发窗口消息前、以及任何直接操作本实例 ImGui 状态的调用前都要先调它。
    void MakeCurrent();

    /// @brief 开始一帧：更新 ImGui 的输入与时间状态。
    void BeginFrame();

    /// @brief 结束一帧：绘制 ImGui 画布并呈现到叠加层。
    void EndFrame();

    /// @brief 取内部 D3D11 设备，供界面层上传纹理（如图标）。
    /// @return Initialize 之前返回 nullptr；调用方不得释放或长期持有该指针。
    ID3D11Device* Device() const { return device_.Get(); }

private:
    bool CreateDevice();
    bool CreateSwapChain();
    bool CreateComposition();
    bool CreateRenderTarget();
    void InitializeImGui();
    void LoadUiFont();
    /// @brief 按当前的 uiScale_ 重建基础样式（先复位再缩放，避免重复累乘）。
    void ApplyStyle();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> compositionDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> compositionTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> compositionVisual_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTargetView_;

    HWND window_ = nullptr;
    ImGuiContext* imguiContext_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    float uiScale_ = 1.0f;
    bool lightTheme_ = false;
    uint32_t accentRgb_ = 0;
    bool imguiContextCreated_ = false;
    bool imguiBackendsInitialized_ = false;
    // 记录已生效的样式参数，重复设置同类值时跳过 ApplyStyle，避免每帧重建样式。
    bool styleApplied_ = false;
};

}  // namespace wind::platform
