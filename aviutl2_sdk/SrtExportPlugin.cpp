//----------------------------------------------------------------------------------
// SRT Export Plugin for AviUtl ExEdit2
//----------------------------------------------------------------------------------
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include "plugin2.h"
#include "logger2.h" // ログ出力用
#include "config2.h" // 設定用

EDIT_HANDLE* edit_handle = nullptr;
LOG_HANDLE* logger = nullptr;
CONFIG_HANDLE* config = nullptr;
HINSTANCE g_hInstance = nullptr;

// プラグインの情報定義
COMMON_PLUGIN_TABLE common_plugin_table = {
    L"SRT Export Plugin",
    L"SRT Export Plugin 1.00 by AI"
};

// 必須バージョン (ExEdit2の適当なバージョン)
EXTERN_C __declspec(dllexport) DWORD RequiredVersion() {
    return 2003300;
}

// ログ用初期化
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) {
    logger = handle;
}

// 設定用初期化
EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) {
    config = handle;
}

// 初期化終了
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) {
    return true;
}

// 終了時
EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

// エクスポート処理コールバック
void ExportSrtCallback(EDIT_SECTION* edit) {
    if (!edit || !edit_handle) {
        MessageBoxW(NULL, L"編集ハンドルの取得に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
        return;
    }

    PROJECT_FILE* proj = edit->get_project_file(edit_handle);
    if (!proj) {
        MessageBoxW(NULL, L"プロジェクトの取得に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
        return;
    }

    LPCWSTR projPath = proj->get_project_file_path();
    if (!projPath || wcslen(projPath) == 0) {
        MessageBoxW(NULL, L"プロジェクトが保存されていません。\nまずはプロジェクトを一度保存（Ctrl+S）してから実行してください。", L"エラー", MB_OK | MB_ICONWARNING);
        return;
    }

    // プラグインのDLLファイルパスから generate_srt.exe のパスを生成する
    WCHAR exePath[MAX_PATH];
    if (GetModuleFileNameW(g_hInstance, exePath, MAX_PATH) == 0) {
        MessageBoxW(NULL, L"DLLのパス取得に失敗しました。", L"エラー", MB_OK | MB_ICONERROR);
        return;
    }

    WCHAR* lastSlash = wcsrchr(exePath, L'\\');
    if (lastSlash) {
        lastSlash[1] = L'\0';
        wcscat_s(exePath, MAX_PATH, L"generate_srt.exe");
    }

    // 引数の構築（プロジェクトファイルのパスを渡す）
    WCHAR args[MAX_PATH + 32];
    swprintf_s(args, MAX_PATH + 32, L"\"%s\"", projPath);

    // generate_srt.exe の実行
    HINSTANCE res = ShellExecuteW(NULL, L"open", exePath, args, NULL, SW_SHOW);
    if ((INT_PTR)res <= 32) {
        WCHAR errorMsg[1024];
        swprintf_s(errorMsg, 1024, L"generate_srt.exe の起動に失敗しました。\nプラグインと同じフォルダに存在するか確認してください。\nパス: %s", exePath);
        MessageBoxW(NULL, errorMsg, L"エラー", MB_OK | MB_ICONERROR);
    }
}

// プラグイン登録処理
EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    if (host) {
        edit_handle = host->create_edit_handle();
        host->register_export_menu(L"SRTエクスポート", ExportSrtCallback);
    }
}

// 汎用プラグインテーブル取得
EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable() {
    return &common_plugin_table;
}

// DLLエントリーポイント
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
