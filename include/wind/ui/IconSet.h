#pragma once

#include <windows.h>

#include <d3d11.h>
#include <imgui.h>

#include <array>

namespace wind::ui {

/// @brief 界面图标的运行时纹理集合。
///
/// 图标以 PNG 形式随资源脚本编译进 exe（见 res/wind.rc），启动时解码成直通 alpha 的位图，
/// 上传成 D3D11 纹理并预先算好 mip 链：绘制时按目标像素尺寸采样对应 mip，
/// 因此 125% / 150% / 200% 缩放下图标都能保持清晰，也不会因为双线性缩小而糊掉。
/// 图标本身只有白色与 alpha，具体颜色由 ImGui 的顶点色染色，随主题与状态（禁用/置顶）变化。
///
/// 生命周期：Load 之后才可 Texture；纹理必须早于 D3D 设备释放，故 Release 或析构都要先于
/// Renderer::Shutdown。纹理属于创建它的 D3D11 设备，跨设备不可共享——工具栏与设置页各用一份
/// 实例，分别在各自渲染器初始化时 Load。
class IconSet {
public:
    enum class Id { Pin, Opacity, Reset, Close };

    IconSet() = default;
    ~IconSet();

    IconSet(const IconSet&) = delete;
    IconSet& operator=(const IconSet&) = delete;

    /// @brief 解码全部内嵌图标并上传纹理。
    /// @param device 渲染器持有的 D3D11 设备；传 nullptr 视为失败。
    /// @note 单个图标失败只记日志，其余图标照常可用；全部失败时 Ready() 返回 false，
    ///       工具栏会退回纯文字标签。
    void Load(ID3D11Device* device);

    /// @brief 释放纹理；重复调用安全。
    void Release();

    /// @brief 是否至少有一枚图标可用（工具栏据此决定画图标还是画文字）。
    bool Ready() const;

    /// @brief 取图标纹理；未加载或该图标失败时返回 0，调用方应跳过绘制。
    ImTextureID Texture(Id id) const;

private:
    struct Slot {
        ID3D11Texture2D* texture = nullptr;
        ID3D11ShaderResourceView* view = nullptr;
    };

    static constexpr size_t kSlotCount = 4;

    std::array<Slot, kSlotCount> slots_{};
};

}  // namespace wind::ui
