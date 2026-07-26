#pragma once
#include "Every_shortcut.h"
#include "resource.h"

namespace SettingDialog {
    INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    INT_PTR CALLBACK CmdEditorDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void ShowSettingWindow(HWND parent, HINSTANCE hinst);
    void RefreshCommandListView(HWND hwndList);
    void ProposeRestartForNewCommand();
    bool OpenCommandEditor(HWND parent, ShortcutCommand* cmd);
}
