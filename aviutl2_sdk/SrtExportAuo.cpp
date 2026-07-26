//----------------------------------------------------------------------------------
// SRT Export Output Plugin for AviUtl ExEdit2
//----------------------------------------------------------------------------------
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <sstream>
#include "output2.h"
#include "plugin2.h" // For PROJECT_FILE
#include "Generator.h"
#include "resource.h"

HINSTANCE g_hInstance = nullptr;
WCHAR g_project_path[MAX_PATH] = {0};

Config g_config;

void LoadIni() {
    WCHAR iniPath[MAX_PATH];
    GetModuleFileNameW(g_hInstance, iniPath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(iniPath, L'\\');
    if (lastSlash) {
        lastSlash[1] = L'\0';
        wcscat_s(iniPath, MAX_PATH, L"SrtExport.ini");
    }

    g_config.add_name_mode = GetPrivateProfileIntW(L"Settings", L"AddNameMode", 2, iniPath);
    
    WCHAR buf[1024];
    GetPrivateProfileStringW(L"Settings", L"NameFormat", L"【{name}】 {text}", buf, 1024, iniPath);
    g_config.name_format = buf;
}

void SaveIni() {
    WCHAR iniPath[MAX_PATH];
    GetModuleFileNameW(g_hInstance, iniPath, MAX_PATH);
    WCHAR* lastSlash = wcsrchr(iniPath, L'\\');
    if (lastSlash) {
        lastSlash[1] = L'\0';
        wcscat_s(iniPath, MAX_PATH, L"SrtExport.ini");
    }

    WCHAR modeStr[16];
    swprintf_s(modeStr, 16, L"%d", g_config.add_name_mode);
    WritePrivateProfileStringW(L"Settings", L"AddNameMode", modeStr, iniPath);
    WritePrivateProfileStringW(L"Settings", L"NameFormat", g_config.name_format.c_str(), iniPath);
}

INT_PTR CALLBACK ConfigDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
        {
            LoadIni();
            int radioId = IDC_RADIO_CHANGE;
            if (g_config.add_name_mode == 0) radioId = IDC_RADIO_ALL;
            else if (g_config.add_name_mode == 1) radioId = IDC_RADIO_FIRST;
            else if (g_config.add_name_mode == 3) radioId = IDC_RADIO_NONE;
            CheckRadioButton(hwnd, IDC_RADIO_ALL, IDC_RADIO_NONE, radioId);

            SetDlgItemTextW(hwnd, IDC_EDIT_FORMAT, g_config.name_format.c_str());
            return TRUE;
        }
        case WM_COMMAND:
        {
            if (LOWORD(wParam) == IDOK) {
                if (IsDlgButtonChecked(hwnd, IDC_RADIO_ALL) == BST_CHECKED) g_config.add_name_mode = 0;
                else if (IsDlgButtonChecked(hwnd, IDC_RADIO_FIRST) == BST_CHECKED) g_config.add_name_mode = 1;
                else if (IsDlgButtonChecked(hwnd, IDC_RADIO_CHANGE) == BST_CHECKED) g_config.add_name_mode = 2;
                else g_config.add_name_mode = 3;

                WCHAR buf[1024];
                GetDlgItemTextW(hwnd, IDC_EDIT_FORMAT, buf, 1024);
                g_config.name_format = buf;

                SaveIni();
                MessageBoxW(hwnd, L"設定を保存しました。", L"完了", MB_OK | MB_ICONINFORMATION);
                EndDialog(hwnd, IDOK);
                return TRUE;
            }
            else if (LOWORD(wParam) == IDCANCEL) {
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

bool ShowConfig(HWND hwnd, HINSTANCE dll_hinst) {
    DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_CONFIG_DIALOG), hwnd, ConfigDlgProc, 0);
    return true;
}

bool OutputCallback(OUTPUT_INFO* oip) {
    if (wcslen(g_project_path) == 0) {
        MessageBoxW(NULL, L"プロジェクトパスが不明です。\nまずはプロジェクトを一度保存（Ctrl+S）してから実行してください。", L"エラー", MB_OK | MB_ICONWARNING);
        return false;
    }

    oip->func_rest_time_disp(10, 100);
    LoadIni();

    oip->func_rest_time_disp(50, 100);
    bool success = GenerateSrt(g_project_path, oip->savefile, g_config);

    oip->func_rest_time_disp(100, 100);

    if (!success) {
        MessageBoxW(NULL, L"SRTの生成中にエラーが発生しました。\nプロジェクトファイルが読み込めないか、出力先に書き込めません。", L"エラー", MB_OK | MB_ICONERROR);
        return false;
    }

    MessageBoxW(NULL, L"SRTの出力が完了しました！", L"完了通知", MB_OK | MB_ICONINFORMATION);
    return true;
}

bool LoadProjectConfig(PROJECT_FILE* project) {
    if (project) {
        LPCWSTR path = project->get_project_file_path();
        if (path) {
            wcscpy_s(g_project_path, MAX_PATH, path);
        }
    }
    return true;
}

bool SaveProjectConfig(PROJECT_FILE* project) {
    if (project) {
        LPCWSTR path = project->get_project_file_path();
        if (path) {
            wcscpy_s(g_project_path, MAX_PATH, path);
        }
    }
    return true;
}

OUTPUT_PLUGIN_TABLE output_plugin_table = {
    OUTPUT_PLUGIN_TABLE::FLAG_IMAGE | OUTPUT_PLUGIN_TABLE::FLAG_PROJECT_CONFIG,
    L"セリフ準備SRT出力",
    L"SRTファイル (*.srt)\0*.srt\0",
    L"SRT Export by AI",
    OutputCallback,
    ShowConfig,
    nullptr,
    LoadProjectConfig,
    SaveProjectConfig
};

EXTERN_C __declspec(dllexport) OUTPUT_PLUGIN_TABLE* __stdcall GetOutputPluginTable(void) {
    return &output_plugin_table;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        g_hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}
