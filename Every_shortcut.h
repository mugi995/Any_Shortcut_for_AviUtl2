#pragma once

#include <windows.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>

#include "plugin2.h"
#include "logger2.h"
#include "config2.h"

// アクション種別
enum class ActionType : int {
    DROP_OBJECT  = 1,
    ADD_FILTER   = 2,
    SET_PARAM    = 3,
    COMBO        = 4,
    MENU_COMMAND = 5,
};

// 単一アクションステップ
struct ActionStep {
    ActionType type;
    std::wstring target_name;
    std::wstring param_name;
    std::wstring param_value;
    int filter_cmd_id = 0;
};

// ショートカットコマンド定義
struct ShortcutCommand {
    int id;
    std::wstring name;
    std::wstring category;
    std::vector<ActionStep> steps;
    int virtual_key = 0;
    int modifiers = 0;
};

// グローバル変数宣言
extern EDIT_HANDLE* g_edit_handle;
extern LOG_HANDLE* g_logger;
extern CONFIG_HANDLE* g_config;
extern HOST_APP_TABLE* g_host;
extern HINSTANCE g_hInstance;
extern HWND g_setting_hwnd;
extern std::vector<ShortcutCommand> g_commands;

// 共通ユーティリティ
std::string  WStringToString(const std::wstring& wstr);
std::wstring StringToWString(const std::string& str);

// ConfigManager
namespace ConfigManager {
    bool LoadConfig();
    bool SaveConfig();
    std::wstring GetConfigFilePath();
    std::wstring GetDllFolderPath();
    ShortcutCommand* FindCommandById(int id);
    int GenerateNewCommandId();
    void CreateDefaultCommands();
}

// CommandExecutor
namespace CommandExecutor {
    bool ExecuteCommand(const ShortcutCommand& cmd, EDIT_SECTION* edit);
    bool ExecuteCommandById(int id);
    bool ExecuteCommandInSection(int id, EDIT_SECTION* edit);
    void ScanMenuCommands(std::vector<std::pair<std::wstring, int>>& out);

    bool AddFilterByAlias(EDIT_SECTION* edit, OBJECT_HANDLE obj, const std::wstring& effect_name);
}

// SettingDialog
namespace SettingDialog {
    INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void ShowSettingWindow(HWND parent, HINSTANCE hinst);
    void RefreshCommandListView(HWND hwndList);
    void ProposeRestartForNewCommand();
    bool OpenCommandEditor(HWND parent, ShortcutCommand* cmd);
}
