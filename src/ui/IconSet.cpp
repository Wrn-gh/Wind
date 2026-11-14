#include "wind/ui/IconSet.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "resource.h"
#include "wind/platform/Win32Error.h"

namespace wind::ui {
namespace {

using Microsoft::WRL::ComPtr;
using wind::platform::HResultMessage;
using wind::platform::Log;

/// FindResourceW 的第二个类型参数：预定义资源类型 RT_RCDATA 的数值。
/// 直接用 RT_RCDATA 宏会得到 char*，与宽字符重载不匹配，因此这里用数值常量。
constexpr WORD kResourceTypeRcData = 10;

constexpr int kResourceIds[] = {
    IDR_WIND_ICON_PIN,
    IDR_WIND_ICON_OPACITY,
    IDR_WIND_ICON_RESET,
    IDR_WIND_ICON_CLOSE,
};
constexpr const char* kResourceNames[] = {"pin", "opacity", "reset", "close"};

/// @brief 从 exe 自己的资源段里取出二进制块。
bool ReadResource(int resourceId, std::vector<uint8_t>& bytes) {
    HINSTANCE instance = ::GetModuleHandleW(nullptr);
    HRSRC handle = ::FindResourceW(instance, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(kResourceTypeRcData));
    if (handle == nullptr) {
        Log("图标资源 %d 不存在，错误码 %lu", resourceId, ::GetLastError());
        return false;
    }

    const DWORD size = ::SizeofResource(instance, handle);
    HGLOBAL loaded = ::LoadResource(instance, handle);
    if (size == 0 || loaded == nullptr) {
        Log("图标资源 %d 加载失败，错误码 %lu", resourceId, ::GetLastError());
        return false;
    }

    const void* data = ::LockResource(loaded);
    if (data == nullptr) {
        Log("图标资源 %d 锁定失败", resourceId);
        return false;
    }

    const auto* first = static_cast<const uint8_t*>(data);
    bytes.assign(first, first + size);
    return true;
}

/// @brief RAII：只为创建 WIC 对象而初始化 COM，作用域结束即还原。
class ComScope {
public:
    ComScope() : result_(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope() {
        if (result_ == S_OK || result_ == S_FALSE) {
            ::CoUninitialize();
        }
    }

    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    bool Usable() const {
        // RPC_E_CHANGED_MODE 说明本线程已经按别的模型初始化过 COM，此时 WIC 依然可用。
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT result_ = E_FAIL;
};

/// @brief 把 PNG 字节解码成 32bpp BGRA 直通 alpha 位图。
bool DecodePng(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& pixels, UINT& width, UINT& height) {
    HGLOBAL memory = ::GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (memory == nullptr) {
        return false;
    }
    void* destination = ::GlobalLock(memory);
    if (destination == nullptr) {
        ::GlobalFree(memory);
        return false;
    }
    std::memcpy(destination, bytes.data(), bytes.size());
    ::GlobalUnlock(memory);

    ComPtr<IStream> stream;
    HRESULT hr = ::CreateStreamOnHGlobal(memory, TRUE, stream.GetAddressOf());
    if (FAILED(hr)) {
        ::GlobalFree(memory);
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    hr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        Log("创建 WIC 工厂失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
    if (FAILED(hr)) {
        Log("PNG 解码器打开图标失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }
    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        Log("图标像素格式转换失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        return false;
    }

    pixels.resize(static_cast<size_t>(width) * height * 4);
    hr = converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr)) {
        Log("图标像素读取失败：%s", HResultMessage(hr).c_str());
        return false;
    }
    return true;
}

/// @brief 逐级折半做盒式滤波，得到完整的 mip 链（第 0 级是原图）。
///
/// 自己在 CPU 上算而不是用 GenerateMips，是为了不额外要求纹理带 render target 绑定；
/// 盒式平均也正是缩小时该用的滤波，透明度不会因为采样点不足而闪烁。
std::vector<std::vector<uint8_t>> BuildMips(const std::vector<uint8_t>& base, UINT width, UINT height) {
    std::vector<std::vector<uint8_t>> mips;
    mips.push_back(base);

    UINT currentWidth = width;
    UINT currentHeight = height;
    while (currentWidth > 1 || currentHeight > 1) {
        const UINT nextWidth = std::max(1u, currentWidth / 2);
        const UINT nextHeight = std::max(1u, currentHeight / 2);
        const std::vector<uint8_t>& source = mips.back();
        std::vector<uint8_t> next(static_cast<size_t>(nextWidth) * nextHeight * 4, 0);

        for (UINT y = 0; y < nextHeight; ++y) {
            for (UINT x = 0; x < nextWidth; ++x) {
                const UINT x0 = std::min(x * 2, currentWidth - 1);
                const UINT x1 = std::min(x0 + 1, currentWidth - 1);
                const UINT y0 = std::min(y * 2, currentHeight - 1);
                const UINT y1 = std::min(y0 + 1, currentHeight - 1);
                for (size_t channel = 0; channel < 4; ++channel) {
                    const size_t i00 = (static_cast<size_t>(y0) * currentWidth + x0) * 4 + channel;
                    const size_t i10 = (static_cast<size_t>(y0) * currentWidth + x1) * 4 + channel;
                    const size_t i01 = (static_cast<size_t>(y1) * currentWidth + x0) * 4 + channel;
                    const size_t i11 = (static_cast<size_t>(y1) * currentWidth + x1) * 4 + channel;
                    const unsigned sum = source[i00] + source[i10] + source[i01] + source[i11];
                    next[(static_cast<size_t>(y) * nextWidth + x) * 4 + channel] = static_cast<uint8_t>((sum + 2) / 4);
                }
            }
        }

        mips.push_back(std::move(next));
        currentWidth = nextWidth;
        currentHeight = nextHeight;
    }
    return mips;
}

/// @brief 把 BGRA 位图连同 mip 链上传成纹理与着色器资源视图。
bool CreateTexture(ID3D11Device* device, const std::vector<uint8_t>& pixels, UINT width, UINT height,
                   ID3D11Texture2D** texture, ID3D11ShaderResourceView** view) {
    const std::vector<std::vector<uint8_t>> mips = BuildMips(pixels, width, height);

    D3D11_TEXTURE2D_DESC description{};
    description.Width = width;
    description.Height = height;
    description.MipLevels = static_cast<UINT>(mips.size());
    description.ArraySize = 1;
    description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.Usage = D3D11_USAGE_DEFAULT;
    description.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&description, nullptr, texture);
    if (FAILED(hr)) {
        Log("创建图标纹理失败：%s", HResultMessage(hr).c_str());
        return false;
    }

    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.GetAddressOf());
    if (context == nullptr) {
        return false;
    }

    UINT levelWidth = width;
    UINT levelHeight = height;
    for (size_t level = 0; level < mips.size(); ++level) {
        context->UpdateSubresource(*texture, static_cast<UINT>(level), nullptr, mips[level].data(), levelWidth * 4, 0);
        levelWidth = std::max(1u, levelWidth / 2);
        levelHeight = std::max(1u, levelHeight / 2);
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC viewDescription{};
    viewDescription.Format = description.Format;
    viewDescription.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    viewDescription.Texture2D.MostDetailedMip = 0;
    viewDescription.Texture2D.MipLevels = static_cast<UINT>(mips.size());

    hr = device->CreateShaderResourceView(*texture, &viewDescription, view);
    if (FAILED(hr)) {
        Log("创建图标纹理视图失败：%s", HResultMessage(hr).c_str());
        return false;
    }
    return true;
}

}  // namespace

IconSet::~IconSet() {
    Release();
}

void IconSet::Load(ID3D11Device* device) {
    Release();
    if (device == nullptr) {
        Log("图标加载缺少 D3D11 设备，工具栏退回文字标签");
        return;
    }

    const ComScope com;
    if (!com.Usable()) {
        Log("COM 初始化失败，图标无法解码，工具栏退回文字标签");
        return;
    }

    for (size_t index = 0; index < slots_.size(); ++index) {
        std::vector<uint8_t> bytes;
        if (!ReadResource(kResourceIds[index], bytes)) {
            continue;
        }

        std::vector<uint8_t> pixels;
        UINT width = 0;
        UINT height = 0;
        if (!DecodePng(bytes, pixels, width, height)) {
            Log("图标 %s 解码失败", kResourceNames[index]);
            continue;
        }

        Slot& slot = slots_[index];
        if (!CreateTexture(device, pixels, width, height, &slot.texture, &slot.view)) {
            if (slot.texture != nullptr) {
                slot.texture->Release();
                slot.texture = nullptr;
            }
            continue;
        }
    }

    // 图标要同步出现才协调，缺一枚就整体退回文字标签，避免工具栏一半图标一半文字。
    for (const Slot& slot : slots_) {
        if (slot.view == nullptr) {
            Log("图标不全，工具栏整体退回文字标签");
            Release();
            return;
        }
    }
}

void IconSet::Release() {
    for (Slot& slot : slots_) {
        if (slot.view != nullptr) {
            slot.view->Release();
            slot.view = nullptr;
        }
        if (slot.texture != nullptr) {
            slot.texture->Release();
            slot.texture = nullptr;
        }
    }
}

bool IconSet::Ready() const {
    for (const Slot& slot : slots_) {
        if (slot.view != nullptr) {
            return true;
        }
    }
    return false;
}

ImTextureID IconSet::Texture(Id id) const {
    const size_t index = static_cast<size_t>(id);
    if (index >= slots_.size() || slots_[index].view == nullptr) {
        return 0;
    }
    return reinterpret_cast<ImTextureID>(slots_[index].view);
}

}  // namespace wind::ui
