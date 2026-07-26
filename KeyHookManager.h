#pragma once
#include "Every_shortcut.h"

namespace KeyHookManager {
    LRESULT CALLBACK ShortcutEditSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void SetSuppressDispatch(bool suppress);
}
