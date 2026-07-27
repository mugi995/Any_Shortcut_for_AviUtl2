#include "CommandExecutor.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <map>
#include <regex>

// 共通ユーティリティ実装
std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string result(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &result[0], len, nullptr, nullptr);
    return result;
}

std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], len);
    return result;
}

namespace CommandExecutor {

    void InitFilterCommandCache() {}

    static void ScanMenuRecursive(HMENU hMenu, const std::wstring& prefix,
                                   std::vector<std::pair<std::wstring, int>>& out) {
        if (!hMenu) return;
        int count = GetMenuItemCount(hMenu);
        for (int i = 0; i < count; i++) {
            WCHAR buf[512] = {0};
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_ID;
            mii.dwTypeData = buf;
            mii.cch = 511;
            if (!GetMenuItemInfoW(hMenu, i, TRUE, &mii)) continue;

            std::wstring name(buf);
            name.erase(std::remove(name.begin(), name.end(), L'&'), name.end());

            if (mii.hSubMenu) {
                ScanMenuRecursive(mii.hSubMenu, prefix + name + L"\\", out);
            } else if (mii.wID > 0 && !name.empty()) {
                out.push_back({prefix + name, (int)mii.wID});
            }
        }
    }

    static HWND g_scan_skip_hwnd = nullptr;

    static BOOL CALLBACK ScanMenuEnumProc(HWND hwnd, LPARAM lParam) {
        if (hwnd == g_scan_skip_hwnd) return TRUE;
        auto* vec = (std::vector<std::pair<std::wstring, int>>*)lParam;
        HMENU hMenu = GetMenu(hwnd);
        if (hMenu) ScanMenuRecursive(hMenu, L"", *vec);
        return TRUE;
    }

    void ScanMenuCommands(std::vector<std::pair<std::wstring, int>>& out) {
        out.clear();
        HWND hMain = g_edit_handle ? g_edit_handle->get_host_app_window() : nullptr;
        g_scan_skip_hwnd = hMain;
        if (hMain) {
            HMENU hMenu = GetMenu(hMain);
            ScanMenuRecursive(hMenu, L"", out);
        }
        EnumThreadWindows(GetCurrentThreadId(), ScanMenuEnumProc, (LPARAM)&out);
    }

    // エイリアスデータにエフェクト定義をテキストとして追加
    std::string AppendEffectToAlias(const std::string& alias_utf8, const std::wstring& effect_name) {
        std::string effect_name_utf8 = WStringToString(effect_name);
        std::string result = alias_utf8;

        // 末尾の改行を整える
        if (!result.empty() && result.back() != '\n') result += '\n';

        int effect_num = 0;
        size_t pos = 0;
        while ((pos = result.find("[Object.", pos)) != std::string::npos) {
            effect_num++;
            pos += 8;
        }

        // 最後のオブジェクトセクションにエフェクトを追加
        result += "[Object." + std::to_string(effect_num) + "]\n";
        result += "effect.name=" + effect_name_utf8 + "\n";

        return result;
    }

    // エイリアスベースのフィルタ追加（WM_COMMAND フォールバック用）
    // 選択オブジェクトのエイリアスにフィルタ定義を追記して再生成する
    bool AddFilterByAlias(EDIT_SECTION* edit, OBJECT_HANDLE obj, const std::wstring& effect_name) {
        if (!edit || !obj) return false;

        LPCSTR orig_alias = edit->get_object_alias(obj);
        if (!orig_alias) return false;

        int num_sel = edit->get_selected_object_num();

        // 位置情報を取得
        OBJECT_LAYER_FRAME lf = edit->get_object_layer_frame(obj);
        int layer = lf.layer;
        int frame = lf.start;
        int length = lf.end - lf.start + 1;

        std::string orig_alias_str(orig_alias);
        std::string new_alias = AppendEffectToAlias(orig_alias_str, effect_name);

        if (g_logger) {
            g_logger->verbose(g_logger, L"[AddFilterByAlias] Attempting alias-based filter addition");
        }

        // 元オブジェクトを削除し、フィルタ追記済みエイリアスで再生成
        edit->delete_object(obj);
        OBJECT_HANDLE new_obj = edit->create_object_from_alias(new_alias.c_str(), layer, frame, length);
        if (new_obj) {
            edit->set_focus_object(new_obj);
            if (g_logger) {
                g_logger->log(g_logger, (L"[AddFilterByAlias] Filter added via alias: " + effect_name).c_str());
            }
            return true;
        }

        // 失敗時: 元のエイリアスでオブジェクトを復元
        OBJECT_HANDLE restored = edit->create_object_from_alias(orig_alias_str.c_str(), layer, frame, length);
        if (restored) {
            edit->set_focus_object(restored);
        }
        if (g_logger) {
            g_logger->warn(g_logger, L"[AddFilterByAlias] Alias-based filter addition failed, object restored");
        }
        return false;
    }

    // エイリアスファイルを読み込んで UTF-8 文字列として返す
    static std::string ReadAliasFile(const std::wstring& path) {
        std::wstring real_path = path;
        size_t pos = real_path.find(L"%Alias%");
        if (pos != std::wstring::npos) {
            std::wstring alias_name = real_path.substr(pos + 7);

            // DLL 同階層の alias フォルダ
            WCHAR mod_path[MAX_PATH] = {0};
            if (g_hInstance && GetModuleFileNameW(g_hInstance, mod_path, MAX_PATH)) {
                std::wstring dir = mod_path;
                size_t slash = dir.find_last_of(L"\\/");
                if (slash != std::wstring::npos) dir = dir.substr(0, slash);
                real_path = dir + L"\\alias" + alias_name;
            }

            std::ifstream ifs1(real_path, std::ios::binary);
            if (ifs1.is_open()) {
                return std::string((std::istreambuf_iterator<char>(ifs1)), std::istreambuf_iterator<char>());
            }

            // AviUtl2 本体の alias フォルダにもフォールバック
            if (g_config && g_config->app_data_path) {
                std::wstring config_alias = std::wstring(g_config->app_data_path) + L"\\alias" + alias_name;
                std::ifstream ifs2(config_alias, std::ios::binary);
                if (ifs2.is_open()) {
                    return std::string((std::istreambuf_iterator<char>(ifs2)), std::istreambuf_iterator<char>());
                }
            }
            return "";
        }

        std::ifstream ifs(real_path, std::ios::binary);
        if (!ifs.is_open()) return "";
        return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }

    bool ExecuteCommand(const ShortcutCommand& cmd, EDIT_SECTION* edit) {
        if (!edit) return false;

        bool any_success = false;
        for (const auto& step : cmd.steps) {
            switch (step.type) {
                case ActionType::DROP_OBJECT: {
                    int layer = 0, frame = 0;
                    if (step.param_value == L"seek" && edit->info) {
                        layer = edit->info->layer;
                        frame = edit->info->frame;
                    } else if (!step.param_value.empty() && step.param_value != L"mouse") {
                        // 数値レイヤー指定（UIは1-based → SDKは0-based）
                        try {
                            layer = std::stoi(WStringToString(step.param_value)) - 1;
                            if (layer < 0) layer = 0;
                        } catch (...) { layer = 0; }
                        frame = edit->info ? edit->info->frame : 0;
                    } else {
                        if (!edit->get_mouse_layer_frame(&layer, &frame)) {
                            layer = 1;
                            frame = 1;
                        }
                    }
                    OBJECT_HANDLE new_obj = nullptr;

                    bool is_alias_path = step.target_name.find(L".object") != std::wstring::npos ||
                                         step.target_name.find(L"%Alias%") != std::wstring::npos;

                    // フルパス → create_object_from_media_file
                    bool is_file_path = step.target_name.find(L":\\") != std::wstring::npos ||
                                        step.target_name.find(L"\\\\") != std::wstring::npos ||
                                        (step.target_name.size() > 4 && step.target_name[1] == L':');

                    if (is_file_path) {
                        new_obj = edit->create_object_from_media_file(step.target_name.c_str(), layer, frame, 0);
                        if (!new_obj && g_logger) g_logger->warn(g_logger, (L"[DROP] Media file failed: " + step.target_name).c_str());
                    } else if (is_alias_path) {
                        std::string alias_utf8 = ReadAliasFile(step.target_name);
                        if (!alias_utf8.empty()) {
                            new_obj = edit->create_object_from_alias(alias_utf8.c_str(), layer, frame, 0);
                        }
                        // エイリアスが見つからない場合、ファイル名からエフェクト名を抽出してフォールバック
                        if (!new_obj && alias_utf8.empty()) {
                            std::wstring effect_name = step.target_name;
                            size_t slash = effect_name.find_last_of(L"\\/");
                            if (slash != std::wstring::npos) effect_name = effect_name.substr(slash + 1);
                            size_t dot = effect_name.find(L".object");
                            if (dot != std::wstring::npos) effect_name = effect_name.substr(0, dot);
                            if (!effect_name.empty()) {
                                if (g_logger) g_logger->log(g_logger, (L"[DROP] Alias not found, trying create_object: " + effect_name).c_str());
                                new_obj = edit->create_object(effect_name.c_str(), layer, frame, 0);
                            }
                        }
                    } else {
                        new_obj = edit->create_object(step.target_name.c_str(), layer, frame, 0);
                    }
                    if (new_obj) {
                        edit->set_focus_object(new_obj);
                        any_success = true;
                    } else if (g_logger) {
                        g_logger->warn(g_logger, L"[DROP] Object creation failed");
                    }
                    break;
                }
                case ActionType::ADD_FILTER: {
                    int num_sel = edit->get_selected_object_num();
                    OBJECT_HANDLE obj = nullptr;
                    if (num_sel > 0) {
                        obj = edit->get_selected_object(0);
                    } else {
                        obj = edit->get_focus_object();
                    }
                    if (g_logger) {
                        g_logger->log(g_logger, (L"[ADD_FILTER] target=" + step.target_name +
                            L" num_sel=" + std::to_wstring(num_sel) +
                            L" obj=" + (obj ? L"OK" : L"NULL")).c_str());
                    }
                    if (!obj) {
                        if (g_logger) g_logger->warn(g_logger, L"[ADD_FILTER] No object");
                        break;
                    }

                    // WM_COMMAND は編集ロック内でデッドロックするため、エイリアス方式のみ使用
                    if (AddFilterByAlias(edit, obj, step.target_name)) {
                        any_success = true;
                    } else {
                        if (g_logger) g_logger->warn(g_logger, L"[ADD_FILTER] Alias method failed");
                    }
                    break;
                }
                case ActionType::SET_PARAM: {
                    int num_sel = edit->get_selected_object_num();
                    if (g_logger) {
                        g_logger->log(g_logger, (L"[SET_PARAM] target=" + step.target_name +
                            L" param=" + step.param_name + L" val=" + step.param_value +
                            L" num_sel=" + std::to_wstring(num_sel)).c_str());
                    }

                    // 選択オブジェクト + フォーカスオブジェクトの両方を試行
                    std::vector<OBJECT_HANDLE> targets;
                    for (int i = 0; i < num_sel; ++i) {
                        OBJECT_HANDLE obj = edit->get_selected_object(i);
                        if (obj) targets.push_back(obj);
                    }
                    if (targets.empty()) {
                        OBJECT_HANDLE focus = edit->get_focus_object();
                        if (focus) targets.push_back(focus);
                    }

                    for (OBJECT_HANDLE obj : targets) {
                        if (step.param_name == L"ENABLE" || step.param_name == L"\u30C8\u30B0\u30EB") {
                            EFFECT_HANDLE eff = edit->find_effect(obj, step.target_name.c_str());
                            if (eff) {
                                bool cur = edit->get_effect_enable(eff);
                                edit->set_effect_enable(eff, !cur);
                                any_success = true;
                                if (g_logger) g_logger->log(g_logger, L"[SET_PARAM] Effect toggled");
                            }
                        } else {
                            std::string val_utf8 = WStringToString(step.param_value);

                            // 汎用TOGGLEハンドリング（CHECK型など全ON/OFFパラメータ向け）
                            if (_stricmp(val_utf8.c_str(), "TOGGLE") == 0) {
                                LPCSTR cur_val = edit->get_object_item_value(obj, step.target_name.c_str(), step.param_name.c_str());
                                if (cur_val) {
                                    std::string cur(cur_val);
                                    if (cur == "0") val_utf8 = "1";
                                    else if (cur == "1") val_utf8 = "0";
                                    else if (cur == "OFF") val_utf8 = "ON";
                                    else if (cur == "ON") val_utf8 = "OFF";
                                    else val_utf8 = cur;
                                }
                            } else {
                                bool is_add = false, is_sub = false;
                                if (!val_utf8.empty() && val_utf8[0] == '=') {
                                    val_utf8 = val_utf8.substr(1);
                                } else if (!val_utf8.empty() && val_utf8[0] == '+') {
                                    is_add = true; val_utf8 = val_utf8.substr(1);
                                } else if (!val_utf8.empty() && val_utf8[0] == '-') {
                                    is_sub = true; val_utf8 = val_utf8.substr(1);
                                }

                                LPCSTR cur_val = edit->get_object_item_value(obj, step.target_name.c_str(), step.param_name.c_str());
                                if (cur_val) {
                                    std::string cur(cur_val);
                                    size_t comma = cur.find(',');
                                    size_t new_comma = val_utf8.find(',');
                                    if (comma != std::string::npos && new_comma == std::string::npos) {
                                        if (is_add || is_sub) {
                                            std::string first = cur.substr(0, comma);
                                            std::string rest = cur.substr(comma);
                                            try {
                                                double v = std::stod(first) + (is_add ? std::stod(val_utf8) : -std::stod(val_utf8));
                                                val_utf8 = std::to_string(v);
                                                size_t d = val_utf8.find('.');
                                                if (d != std::string::npos) val_utf8 = val_utf8.substr(0, d + 3);
                                            } catch (...) {}
                                            val_utf8 += rest;
                                        } else {
                                            val_utf8 = val_utf8 + cur.substr(comma);
                                        }
                                    } else if (is_add || is_sub) {
                                        try {
                                            double v = std::stod(cur) + (is_add ? std::stod(val_utf8) : -std::stod(val_utf8));
                                            val_utf8 = std::to_string(v);
                                        } catch (...) {}
                                    }
                                }
                            }
                            try {
                                bool ok = edit->set_object_item_value(obj, step.target_name.c_str(), step.param_name.c_str(), val_utf8.c_str());
                                if (g_logger) {
                                    g_logger->log(g_logger, (L"[SET_PARAM] set_object_item_value result=" + std::to_wstring(ok)).c_str());
                                }
                                if (ok) any_success = true;
                            } catch (...) {
                                if (g_logger) {
                                    g_logger->warn(g_logger, L"[SET_PARAM] set_object_item_value threw exception");
                                }
                            }
                        }
                    }
                    break;
                }
                case ActionType::COMBO:
                    break;
                case ActionType::MENU_COMMAND: {
                    int cmd_id = step.filter_cmd_id;
                    if (cmd_id <= 0) {
                        if (g_logger) g_logger->warn(g_logger, L"[MENU_CMD] Invalid command ID");
                        break;
                    }
                    HWND hMain = g_edit_handle ? g_edit_handle->get_host_app_window() : nullptr;
                    if (hMain) {
                        // PostMessage でデッドロック回避
                        PostMessageW(hMain, WM_COMMAND, MAKEWPARAM(cmd_id, 0), 0);
                        any_success = true;
                        if (g_logger) g_logger->log(g_logger, (L"[MENU_CMD] Posted cmd_id=" + std::to_wstring(cmd_id)).c_str());
                    }
                    break;
                }
            }
        }
        if (g_logger) {
            g_logger->log(g_logger, (L"[ExecCmd] " + cmd.name + L" result=" + (any_success ? L"OK" : L"FAIL")).c_str());
        }
        return any_success;
    }

    static void ExecCallback(void* param, EDIT_SECTION* edit) {
        ShortcutCommand* cmd = (ShortcutCommand*)param;
        if (cmd) ExecuteCommand(*cmd, edit);
    }

    bool ExecuteCommandById(int id) {
        ShortcutCommand* cmd = ConfigManager::FindCommandById(id);
        if (!cmd) return false;

        if (g_edit_handle) {
            g_edit_handle->call_edit_section_param((void*)cmd, ExecCallback);
            return true;
        }
        return false;
    }

    // register_edit_menu のコールバックから呼ばれる（既に編集ロック内）
    bool ExecuteCommandInSection(int id, EDIT_SECTION* edit) {
        ShortcutCommand* cmd = ConfigManager::FindCommandById(id);
        if (!cmd) {
            if (g_logger) g_logger->warn(g_logger, L"[ExecInSection] Command not found");
            return false;
        }
        if (!edit) {
            if (g_logger) g_logger->warn(g_logger, L"[ExecInSection] edit is null");
            return false;
        }
        if (g_logger) {
            g_logger->log(g_logger, (L"[ExecInSection] Executing: " + cmd->name).c_str());
        }
        return ExecuteCommand(*cmd, edit);
    }
}
