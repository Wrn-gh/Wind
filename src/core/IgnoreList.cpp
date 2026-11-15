#include "wind/core/IgnoreList.h"

#include <algorithm>
#include <cwctype>
#include <iterator>

#include "wind/platform/ProcessInfo.h"

namespace wind::core {

std::wstring IgnoreRule::Describe() const {
    if (!IsValid()) {
        return {};
    }
    return process + L" · " + className;
}

bool IgnoreList::Sanitize(const IgnoreRule& rule, IgnoreRule* out) {
    if (!rule.IsValid()) {
        return false;
    }
    if (rule.process.size() > kMaxProcessLength || rule.className.size() > kMaxClassLength) {
        return false;
    }
    if (rule.process.find(kSeparator) != std::wstring::npos ||
        rule.className.find(kSeparator) != std::wstring::npos) {
        return false;
    }
    *out = rule;
    // 大小写统一在这里做：进程名来自 API 或配置文件，两边都可能大小写不一致。
    std::transform(out->process.begin(), out->process.end(), out->process.begin(), ::towlower);
    return true;
}

void IgnoreList::Reset(std::vector<IgnoreRule> rules) {
    rules_.clear();
    rules_.reserve(std::min(rules.size(), kMaxRules));
    for (const IgnoreRule& rule : rules) {
        IgnoreRule clean;
        if (!Sanitize(rule, &clean)) {
            continue;
        }
        // 旧配置文件里可能被手工改出重复行，这里顺手去重。
        const bool duplicate = std::find(rules_.begin(), rules_.end(), clean) != rules_.end();
        if (!duplicate) {
            rules_.push_back(std::move(clean));
        }
        if (rules_.size() >= kMaxRules) {
            break;
        }
    }
    RebuildIndex();
}

bool IgnoreList::Add(const IgnoreRule& rule) {
    IgnoreRule clean;
    if (!Sanitize(rule, &clean) || rules_.size() >= kMaxRules) {
        return false;
    }
    if (std::find(rules_.begin(), rules_.end(), clean) != rules_.end()) {
        return false;
    }
    rules_.push_back(std::move(clean));
    RebuildIndex();
    return true;
}

bool IgnoreList::Remove(size_t index) {
    if (index >= rules_.size()) {
        return false;
    }
    rules_.erase(rules_.begin() + static_cast<std::ptrdiff_t>(index));
    RebuildIndex();
    return true;
}

void IgnoreList::Clear() {
    rules_.clear();
    RebuildIndex();
}

void IgnoreList::RebuildIndex() {
    byClass_.clear();
    processCache_.clear();
    for (size_t index = 0; index < rules_.size(); ++index) {
        byClass_[rules_[index].className].push_back(index);
    }
}

std::wstring IgnoreList::WindowClassName(HWND hwnd) {
    if (hwnd == nullptr) {
        return {};
    }
    wchar_t buffer[kMaxClassLength + 1]{};
    const int length = ::GetClassNameW(hwnd, buffer, static_cast<int>(std::size(buffer)));
    if (length <= 0) {
        return {};
    }
    return std::wstring(buffer, static_cast<size_t>(length));
}

std::wstring IgnoreList::WindowProcessName(HWND hwnd) const {
    if (hwnd == nullptr) {
        return {};
    }
    DWORD processId = 0;
    ::GetWindowThreadProcessId(hwnd, &processId);
    if (processId == 0) {
        return {};
    }

    const auto cached = processCache_.find(processId);
    if (cached != processCache_.end()) {
        return cached->second;
    }
    std::wstring name = platform::ProcessImageName(processId);
    // 空结果也缓存：高权限窗口每次去 OpenProcess 都会失败，白白拖慢命中测试。
    processCache_[processId] = name;
    return name;
}

bool IgnoreList::Matches(HWND hwnd) const {
    if (rules_.empty() || hwnd == nullptr) {
        return false;
    }

    const std::wstring className = WindowClassName(hwnd);
    if (className.empty()) {
        return false;
    }
    const auto index = byClass_.find(className);
    if (index == byClass_.end()) {
        return false;
    }

    const std::wstring process = WindowProcessName(hwnd);
    if (process.empty()) {
        // 高权限窗口取不到进程名时宁可不忽略：静默失效比偶尔选到它更容易发现。
        return false;
    }
    for (const size_t ruleIndex : index->second) {
        if (rules_[ruleIndex].process == process) {
            return true;
        }
    }
    return false;
}

bool MakeIgnoreRule(HWND hwnd, IgnoreRule* out) {
    if (hwnd == nullptr || out == nullptr) {
        return false;
    }
    std::wstring className = IgnoreList::WindowClassName(hwnd);
    if (className.empty()) {
        return false;
    }
    DWORD processId = 0;
    ::GetWindowThreadProcessId(hwnd, &processId);
    std::wstring process = platform::ProcessImageName(processId);
    if (process.empty()) {
        return false;
    }
    out->process = std::move(process);
    out->className = std::move(className);
    return true;
}

}  // namespace wind::core
