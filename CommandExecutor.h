#pragma once
#include "Every_shortcut.h"

namespace CommandExecutor {
    // 内部向け: エイリアスデータにエフェクト定義を追加
    std::string AppendEffectToAlias(const std::string& alias_utf8, const std::wstring& effect_name);
}
