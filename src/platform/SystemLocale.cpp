#include "wind/platform/SystemLocale.h"

#include <windows.h>

namespace wind::platform {

bool SystemUsesChineseUi() {
    // GetUserDefaultUILanguage 会跟着「首选 UI 语言」走；老系统上它也可能回落到
    // 系统安装语言，两者对界面语言的选择都合适。
    const LANGID language = ::GetUserDefaultUILanguage();
    return PRIMARYLANGID(language) == LANG_CHINESE;
}

}  // namespace wind::platform
