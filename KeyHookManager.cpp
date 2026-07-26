#include "KeyHookManager.h"
#include <windows.h>
#include <commctrl.h>
#include <sstream>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

namespace KeyHookManager {

    static HHOOK g_hKbdHook = nullptr;
    static HHOOK g_hCbtHook = nullptr;
    static const UINT_PTR SUBCLASS_ID = 9991;
    static bool g_suppress_dispatch = false;
    static HWND g_shortcut_dlg = nullptr;

    bool IsAviUtlActive() {
        HWND hFg = GetForegroundWindow();
        if (!hFg) return false;
        if (g_edit_handle && hFg == g_edit_handle->get_host_app_window()) return true;
        DWORD fg_pid = 0;
        GetWindowThreadProcessId(hFg, &fg_pid);
        return (fg_pid == GetCurrentProcessId());
    }

    // ショートカット設定ダイアログ内の入力コントロール用サブクラス
    LRESULT CALLBACK ShortcutEditSubclassProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData) {
        switch (uMsg) {
            case WM_GETDLGCODE: {
                LRESULT res = DefSubclassProc(hwnd, uMsg, wParam, lParam);
                return res | DLGC_WANTALLKEYS | DLGC_WANTCHARS;
            }
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN: {
                if (wParam >= VK_F13 && wParam <= VK_F24) {
                    int fk_num = (int)(wParam - VK_F13) + 13;
                    WORD vk_low = LOWORD(wParam);
                    SendMessageW(hwnd, HKM_SETHOTKEY, MAKEWORD(vk_low, 0), 0);

                    WCHAR szKeyName[32];
                    swprintf_s(szKeyName, 32, L"F%d", fk_num);
                    SetWindowTextW(hwnd, szKeyName);

                    HWND hParent = GetParent(hwnd);
                    if (hParent) {
                        int ctrl_id = GetDlgCtrlID(hwnd);
                        SendMessageW(hParent, WM_COMMAND, MAKEWPARAM(ctrl_id, EN_CHANGE), (LPARAM)hwnd);
                    }
                    return 0;
                }
                break;
            }
        }
        return DefSubclassProc(hwnd, uMsg, wParam, lParam);
    }

    static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
        WCHAR szClass[128] = {0};
        GetClassNameW(hwnd, szClass, 127);
        if (_wcsicmp(szClass, L"Edit") == 0 ||
            _wcsicmp(szClass, L"msctls_hotkey32") == 0 ||
            _wcsicmp(szClass, L"SysListView32") == 0) {
            SetWindowSubclass(hwnd, ShortcutEditSubclassProc, SUBCLASS_ID, 0);
        }
        return TRUE;
    }

    static LRESULT CALLBACK CbtProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HCBT_CREATEWND || nCode == HCBT_ACTIVATE) {
            HWND hwnd = (HWND)wParam;
            WCHAR szTitle[256] = {0};
            GetWindowTextW(hwnd, szTitle, 255);
            if (wcsstr(szTitle, L"ショートカット") != nullptr) {
                g_suppress_dispatch = true;
                g_shortcut_dlg = hwnd;
                EnumChildWindows(hwnd, EnumChildProc, 0);
            }
        } else if (nCode == HCBT_DESTROYWND) {
            if ((HWND)wParam == g_shortcut_dlg) {
                g_suppress_dispatch = false;
                g_shortcut_dlg = nullptr;
            }
        }
        return CallNextHookEx(g_hCbtHook, nCode, wParam, lParam);
    }

    // WH_KEYBOARD フック（スレッド単位、PowerToys と競合しない）
    // WH_KEYBOARD: wParam=仮想キーコード, lParam bit31=0でキー押下
    static LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode == HC_ACTION) {
            bool isKeyDown = ((lParam & 0x80000000) == 0);
            if (isKeyDown && !g_suppress_dispatch && !g_setting_hwnd) {
                DWORD vkCode = (DWORD)wParam;
                if (vkCode >= VK_F13 && vkCode <= VK_F24) {
                    if (IsAviUtlActive()) {
                        int mods = 0;
                        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
                        if (GetAsyncKeyState(VK_SHIFT)   & 0x8000) mods |= MOD_SHIFT;
                        if (GetAsyncKeyState(VK_MENU)    & 0x8000) mods |= MOD_ALT;

                        for (const auto& cmd : g_commands) {
                            if (cmd.virtual_key == (int)vkCode && cmd.modifiers == mods) {
                                CommandExecutor::ExecuteCommandById(cmd.id);
                                return 1;
                            }
                        }
                    }
                }
            }
        }
        return CallNextHookEx(g_hKbdHook, nCode, wParam, lParam);
    }

    void InstallHooks() {
        // WH_KEYBOARD（スレッド単位）: PowerToys等の他アプリに影響しない
        if (!g_hKbdHook && g_edit_handle) {
            HWND hMain = g_edit_handle->get_host_app_window();
            if (hMain) {
                DWORD tid = GetWindowThreadProcessId(hMain, nullptr);
                g_hKbdHook = SetWindowsHookExW(WH_KEYBOARD, KeyboardProc, g_hInstance, tid);
            }
        }
        // CBT: ショートカット設定ダイアログのサブクラス化用
        if (!g_hCbtHook) {
            g_hCbtHook = SetWindowsHookExW(WH_CBT, CbtProc, nullptr, GetCurrentThreadId());
        }
    }

    void UninstallHooks() {
        if (g_hKbdHook) { UnhookWindowsHookEx(g_hKbdHook); g_hKbdHook = nullptr; }
        if (g_hCbtHook) { UnhookWindowsHookEx(g_hCbtHook); g_hCbtHook = nullptr; }
        g_suppress_dispatch = false;
        g_shortcut_dlg = nullptr;
    }

    void SetSuppressDispatch(bool suppress) {
        if (!g_suppress_dispatch) g_suppress_dispatch = suppress;
    }

    void ClearSuppressDispatch() {
        g_suppress_dispatch = false;
    }
}
