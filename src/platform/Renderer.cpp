#include "wind/platform/Renderer.h"

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include "wind/platform/Win32Error.h"

namespace wind::platform {
namespace {

/// 中文字体候选（按优先级）：微软雅黑 -> 黑体 -> 回落 ImGui 内置字体。
constexpr const char* kFontCandidates[] = {
    "C:\\Windows\\Fonts\\msyh.ttc",
    "C:\\Windows\\Fonts\\msyh.ttf",
    "C:\\Windows\\Fonts\\simhei.ttf",
};

constexpr float kUiFontSize = 18.0f;

}  // namespace

Renderer::~Renderer() {
    Shutdown();
}

bool Renderer::Initialize(HWND targetWindow, int width, int height) {
    if (targetWindow == nullptr || width <= 0 || height <= 0) {
        Log("Renderer::Initialize 参数非法：window=%p, %dx%d", static_cast<void*>(targetWindow), width, height);
        return false;
    }

    window_ = targetWindow;
    width_ = width;
    height_ = height;

    if (!CreateDevice() || !CreateSwapChain() || !CreateComposition() || !CreateRenderTarget()) {
        return false;
    }
    InitializeImGui();
    return true;
}

bool Renderer::CreateDevice() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if !defined(NDEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    Microsoft::WRL::ComPtr<ID3D11Device> device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                     D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, context.GetAddressOf());

    if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) != 0) {
        // 调试层未安装时退回普通设备，避免开发机之外无法启动。
        flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0, D3D11_SDK_VERSION,
                                 device.GetAddressOf(), nullptr, context.GetAddressOf());
    }

    if (FAILED(hr)) {
        Log("D3D11CreateDevice 失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    device_ = device;
    context_ = context;
    return true;
}

bool Renderer::CreateSwapChain() {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) {
        Log("获取 IDXGIDevice 失败");
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgiDevice->GetAdapter(adapter.GetAddressOf()))) {
        Log("获取 IDXGIAdapter 失败");
        return false;
    }

    Microsoft::WRL::ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(factory.GetAddressOf())))) {
        Log("获取 IDXGIFactory2 失败");
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width = static_cast<UINT>(width_);
    description.Height = static_cast<UINT>(height_);
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.Stereo = FALSE;
    description.SampleDesc.Count = 1;
    description.SampleDesc.Quality = 0;
    description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount = 2;
    description.Scaling = DXGI_SCALING_STRETCH;
    description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    // 逐像素半透明的关键：预乘 alpha 的合成交换链交给 DirectComposition 合成。
    description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    const HRESULT hr =
        factory->CreateSwapChainForComposition(device_.Get(), &description, nullptr, swapChain_.GetAddressOf());
    if (FAILED(hr)) {
        Log("CreateSwapChainForComposition 失败：%s", HResultMessage(hr).c_str());
        return false;
    }
    return true;
}

bool Renderer::CreateComposition() {
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    if (FAILED(device_.As(&dxgiDevice))) {
        return false;
    }

    HRESULT hr = ::DCompositionCreateDevice(dxgiDevice.Get(), IID_PPV_ARGS(compositionDevice_.GetAddressOf()));
    if (FAILED(hr)) {
        Log("DCompositionCreateDevice 失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    hr = compositionDevice_->CreateTargetForHwnd(window_, TRUE, compositionTarget_.GetAddressOf());
    if (FAILED(hr)) {
        Log("CreateTargetForHwnd 失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    hr = compositionDevice_->CreateVisual(compositionVisual_.GetAddressOf());
    if (FAILED(hr)) {
        Log("CreateVisual 失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    hr = compositionVisual_->SetContent(swapChain_.Get());
    if (SUCCEEDED(hr)) {
        hr = compositionTarget_->SetRoot(compositionVisual_.Get());
    }
    if (SUCCEEDED(hr)) {
        hr = compositionDevice_->Commit();
    }
    if (FAILED(hr)) {
        Log("DirectComposition 组装失败：%s", HResultMessage(hr).c_str());
        return false;
    }
    return true;
}

bool Renderer::CreateRenderTarget() {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    if (FAILED(hr)) {
        Log("获取交换链后台缓冲失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    hr = device_->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTargetView_.GetAddressOf());
    if (FAILED(hr)) {
        Log("CreateRenderTargetView 失败：%s", HResultMessage(hr).c_str());
        return false;
    }
    return true;
}

void Renderer::InitializeImGui() {
    IMGUI_CHECKVERSION();
    // 每个 Renderer 持有独立上下文，用完恢复调用方原来那个。
    ImGuiContext* previous = ImGui::GetCurrentContext();
    imguiContext_ = ImGui::CreateContext();
    imguiContextCreated_ = true;
    // ImGui 1.92 起 CreateContext 返回前会自行恢复之前的上下文，所以必须显式切到
    // 新建的这个：否则字体加载和 DX11/Win32 后端初始化都会落到另一个渲染器的
    // 上下文上，本上下文只剩默认字体（中文因此渲染成 ???）。
    ImGui::SetCurrentContext(imguiContext_);

    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 不落地 imgui.ini，工具本身无持久化需求
    io.LogFilename = nullptr;

    LoadUiFont();
    ImGui::StyleColorsDark();

    // 基础样式由 ApplyStyle 统一设定并按 DPI 缩放，这里不直接改 style。
    uiScale_ = 1.0f;
    ApplyStyle();

    ImGui_ImplWin32_Init(window_);
    ImGui_ImplDX11_Init(device_.Get(), context_.Get());
    imguiBackendsInitialized_ = true;

    ImGui::SetCurrentContext(previous);
}

void Renderer::MakeCurrent() {
    if (imguiContext_ != nullptr) {
        ImGui::SetCurrentContext(imguiContext_);
    }
}

void Renderer::ApplyStyle() {
    if (imguiContext_ == nullptr) {
        return;
    }
    MakeCurrent();

    ImGuiStyle& style = ImGui::GetStyle();

    // ScaleAllSizes 是在“当前值”上累乘的，必须先回到默认样式再缩放，
    // 否则每调一次 ApplyStyle 尺寸就翻一倍（设置页每帧都会调，几帧内就爆掉）。
    style = ImGuiStyle();

    style.WindowRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 5.0f;
    style.WindowPadding = ImVec2(10.0f, 8.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);

    // 缩放只作用于间距/圆角/边框等尺寸；字体大小由 FontScaleMain 单独放大，
    // 两者相乘后界面在 200% 缩放下才与系统其它窗口的观感一致。
    style.ScaleAllSizes(uiScale_);
    style.FontScaleMain = uiScale_;

    // 配色与尺寸分开维护：ScaleAllSizes 之后重放一次默认配色即可保证两者互不干扰。
    if (lightTheme_) {
        ImGui::StyleColorsLight();
    } else {
        ImGui::StyleColorsDark();
    }

    if (accentRgb_ != 0) {
        const ImVec4 accent = ImVec4(static_cast<float>((accentRgb_ >> 16) & 0xFF) / 255.0f,
                                     static_cast<float>((accentRgb_ >> 8) & 0xFF) / 255.0f,
                                     static_cast<float>(accentRgb_ & 0xFF) / 255.0f, 1.0f);
        ImVec4* colors = style.Colors;
        // 只染少数“选中/激活”类控件，其余保持默认，避免整页跑偏。
        colors[ImGuiCol_CheckMark] = accent;
        colors[ImGuiCol_SliderGrab] = accent;
        colors[ImGuiCol_SliderGrabActive] = accent;
        colors[ImGuiCol_Header] = ImVec4(accent.x, accent.y, accent.z, 0.45f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(accent.x, accent.y, accent.z, 0.65f);
        colors[ImGuiCol_HeaderActive] = accent;
        colors[ImGuiCol_Button] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x, accent.y, accent.z, 0.75f);
        colors[ImGuiCol_ButtonActive] = accent;
    }

    styleApplied_ = true;
}

void Renderer::SetUiScale(float scale) {
    if (!imguiContextCreated_) {
        return;
    }
    const float clamped = (scale > 0.0f) ? scale : 1.0f;
    // 调用方（设置页、叠加层）每帧都会设置一次，值没变就不必重建样式。
    if (styleApplied_ && clamped == uiScale_) {
        return;
    }
    uiScale_ = clamped;
    ApplyStyle();
}

void Renderer::SetTheme(bool light, uint32_t accentRgb) {
    if (!imguiContextCreated_) {
        return;
    }
    if (styleApplied_ && light == lightTheme_ && accentRgb == accentRgb_) {
        return;
    }
    lightTheme_ = light;
    accentRgb_ = accentRgb;
    ApplyStyle();
}


void Renderer::LoadUiFont() {
    ImGuiIO& io = ImGui::GetIO();
    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;

    for (const char* path : kFontCandidates) {
        if (::GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
            continue;
        }
        ImFont* added = io.Fonts->AddFontFromFileTTF(path, kUiFontSize, &config, ranges);
        if (added != nullptr) {
            Log("已加载界面字体：%s", path);
            return;
        }
    }

    Log("未找到中文字体，界面中文将显示为方块");
    io.Fonts->AddFontDefault();
}

void Renderer::BeginFrame() {
    MakeCurrent();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Renderer::EndFrame() {
    MakeCurrent();
    ImGui::Render();

    ID3D11RenderTargetView* target = renderTargetView_.Get();
    const float transparent[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    context_->OMSetRenderTargets(1, &target, nullptr);
    context_->ClearRenderTargetView(target, transparent);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

    const HRESULT hr = swapChain_->Present(1, 0);
    // DXGI_STATUS_OCCLUDED 属于正常情况（叠加层被完全遮挡），不当作错误。
    if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
        Log("Present 失败：%s", HResultMessage(hr).c_str());
    }
}

bool Renderer::Resize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (width == width_ && height == height_) {
        return true;
    }

    width_ = width;
    height_ = height;

    if (context_ != nullptr) {
        context_->OMSetRenderTargets(0, nullptr, nullptr);
        context_->Flush();
    }
    renderTargetView_.Reset();

    // 合成交换链尺寸不可变，只能销毁重建，再挂回已有的合成视觉。
    swapChain_.Reset();
    if (!CreateSwapChain() || !CreateRenderTarget()) {
        return false;
    }

    if (compositionVisual_ != nullptr && compositionDevice_ != nullptr) {
        compositionVisual_->SetContent(swapChain_.Get());
        compositionDevice_->Commit();
    }
    return true;
}

void Renderer::Shutdown() {
    if (imguiBackendsInitialized_) {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        MakeCurrent();
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::SetCurrentContext(previous);
        imguiBackendsInitialized_ = false;
    }
    if (imguiContextCreated_) {
        ImGuiContext* previous = ImGui::GetCurrentContext();
        MakeCurrent();
        ImGui::DestroyContext(imguiContext_);
        imguiContext_ = nullptr;
        imguiContextCreated_ = false;
        // DestroyContext 会把当前上下文置空，恢复成调用方原来那个（可能是另一个渲染器的）。
        ImGui::SetCurrentContext(previous == imguiContext_ ? nullptr : previous);
    }

    renderTargetView_.Reset();
    compositionVisual_.Reset();
    compositionTarget_.Reset();
    compositionDevice_.Reset();
    swapChain_.Reset();
    context_.Reset();
    device_.Reset();

    window_ = nullptr;
    width_ = 0;
    height_ = 0;
}

}  // namespace wind::platform
