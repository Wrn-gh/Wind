#pragma once

#include <windows.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace wind::core {

/// @brief 一条「永不选取」规则：进程可执行文件名 + 窗口类名都相同才算命中。
///
/// 只按类名会误伤所有用同名窗口类的程序（Chrome 的每个窗口都叫 Chrome_WidgetWin_1），
/// 只按进程名又会把该程序的主窗口一起挡掉；用户真正想排除的往往只是其中一个浮层，
/// 所以两者取交集。
struct IgnoreRule {
    /// @brief 小写 exe 文件名，如 L"explorer.exe"。
    std::wstring process;
    /// @brief 窗口类名，如 L"Shell_TrayWnd"。
    std::wstring className;

    bool operator==(const IgnoreRule&) const = default;

    /// @brief 两个字段都非空才算一条可用的规则。
    bool IsValid() const { return !process.empty() && !className.empty(); }

    /// @brief 供界面展示的文本，形如 "explorer.exe · Shell_TrayWnd"。
    std::wstring Describe() const;
};

/// @brief 用户维护的忽略规则集合。
///
/// 命中判定在鼠标每动一下就对本屏几十个候选窗口各做一遍，因此这里不能每次去
/// OpenProcess：先用 GetClassNameW（廉价）查类名索引，只有类名撞上规则时才取进程名，
/// 且进程名按 PID 缓存，Reset() 时整表清空。
class IgnoreList {
public:
    static constexpr size_t kMaxRules = 64;
    /// @brief 规则字段的长度上限；超长一律视为坏数据（配置文件可能被手工改坏）。
    static constexpr size_t kMaxProcessLength = 260;
    static constexpr size_t kMaxClassLength = 256;
    /// @brief 规则在 ini 里的分隔符，进程名与类名都不会包含它。
    static constexpr wchar_t kSeparator = L'|';

    /// @brief 用一批规则重建索引，非法项（空、含分隔符、超长）被静默丢弃。
    void Reset(std::vector<IgnoreRule> rules);

    const std::vector<IgnoreRule>& Rules() const { return rules_; }
    bool Empty() const { return rules_.empty(); }

    /// @brief 追加一条规则；非法或已存在时返回 false。
    bool Add(const IgnoreRule& rule);
    /// @brief 删除指定下标的规则；越界返回 false。
    bool Remove(size_t index);
    void Clear();

    /// @brief 该窗口是否命中任一规则。
    bool Matches(HWND hwnd) const;

    /// @brief 取窗口类名；失败返回空串。
    static std::wstring WindowClassName(HWND hwnd);
    /// @brief 取窗口所属进程的小写 exe 名；失败返回空串。
    std::wstring WindowProcessName(HWND hwnd) const;

private:
    static bool Sanitize(const IgnoreRule& rule, IgnoreRule* out);
    void RebuildIndex();

    std::vector<IgnoreRule> rules_;
    /// 类名 → 规则下标，避免每次判定都线性扫描全部规则。
    std::unordered_map<std::wstring, std::vector<size_t>> byClass_;
    /// PID → 进程名。窗口的归属进程不会中途变化，一次选取期间的缓存始终有效。
    mutable std::unordered_map<DWORD, std::wstring> processCache_;
};

/// @brief 采样一个窗口得到一条规则。
/// @return 取不到进程名（高权限窗口）或类名时返回 false，并保持 out 不变。
bool MakeIgnoreRule(HWND hwnd, IgnoreRule* out);

}  // namespace wind::core
