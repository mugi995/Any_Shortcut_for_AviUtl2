#include "SettingDialog.h"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <sstream>

#pragma comment(lib, "comctl32.lib")

namespace SettingDialog {

    static HWND g_hMainDlg = nullptr;
    static ShortcutCommand* g_editing_cmd = nullptr;

    // アクション種別名
    static const wchar_t* ActionTypeName(ActionType t) {
        switch (t) {
            case ActionType::DROP_OBJECT:  return L"投下";
            case ActionType::ADD_FILTER:   return L"フィルタ追加";
            case ActionType::SET_PARAM:    return L"パラメータ変更";
            case ActionType::COMBO:         return L"コンボ";
            case ActionType::MENU_COMMAND:  return L"メニューコマンド";
            default: return L"?";
        }
    }

    // リストビューの列を設定
    static void InitListViewColumns(HWND hwndList) {
        LVCOLUMNW lvc = { LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, 0 };
        RECT rc; GetClientRect(hwndList, &rc);
        int w = rc.right - rc.left - 20;

        lvc.pszText = (LPWSTR)L"名前";     lvc.cx = w * 35 / 100; ListView_InsertColumn(hwndList, 0, &lvc);
        lvc.pszText = (LPWSTR)L"カテゴリ"; lvc.cx = w * 30 / 100; ListView_InsertColumn(hwndList, 1, &lvc);
        lvc.pszText = (LPWSTR)L"種別";     lvc.cx = w * 35 / 100; ListView_InsertColumn(hwndList, 2, &lvc);
    }

    void RefreshCommandListView(HWND hwndList) {
        ListView_DeleteAllItems(hwndList);
        for (size_t i = 0; i < g_commands.size(); ++i) {
            const auto& cmd = g_commands[i];
            LVITEMW item = { LVIF_TEXT, (int)i, 0 };
            item.pszText = (LPWSTR)cmd.name.c_str();
            int idx = ListView_InsertItem(hwndList, &item);

            ActionType at = cmd.steps.empty() ? ActionType::COMBO : cmd.steps[0].type;
            if (cmd.steps.size() > 1) at = ActionType::COMBO;

            ListView_SetItemText(hwndList, idx, 1, (LPWSTR)cmd.category.c_str());
            ListView_SetItemText(hwndList, idx, 2, (LPWSTR)ActionTypeName(at));
        }
    }

    // コマンド編集ダイアログ
    struct CmdEditCtx {
        ShortcutCommand* cmd;
        bool is_new;
        HWND hStepList;
        HWND hComboType;
        HWND hEditTarget;
        HWND hEditParamName;
        HWND hEditParamValue;
        int editing_step_idx;
    };

    static void RefreshStepList(CmdEditCtx* ctx) {
        HWND hList = ctx->hStepList;
        ListView_DeleteAllItems(hList);
        for (size_t i = 0; i < ctx->cmd->steps.size(); ++i) {
            const auto& st = ctx->cmd->steps[i];
            LVITEMW item = { LVIF_TEXT, (int)i, 0 };
            item.pszText = (LPWSTR)ActionTypeName(st.type);
            int idx = ListView_InsertItem(hList, &item);
            ListView_SetItemText(hList, idx, 1, (LPWSTR)(st.target_name.empty() ? L"-" : st.target_name.c_str()));
            ListView_SetItemText(hList, idx, 2, (LPWSTR)(st.param_name.empty() ? L"-" : st.param_name.c_str()));
            ListView_SetItemText(hList, idx, 3, (LPWSTR)(st.param_value.empty() ? L"-" : st.param_value.c_str()));
        }
    }

    // エフェクト名列挙コールバック
    struct EffectEntry {
        std::wstring name;
        int type;
    };

    static void EnumEffectCallback(void* param, LPCWSTR name, int type, int flag) {
        auto* list = (std::vector<EffectEntry>*)param;
        if (name && wcslen(name) > 0) list->push_back({name, type});
    }

    // エフェクトパラメータ列挙コールバック
    struct ParamEntry {
        std::wstring name;
        int type;
    };

    static void EnumItemCallback(void* param, LPCWSTR name, int type) {
        auto* list = (std::vector<ParamEntry>*)param;
        if (name && wcslen(name) > 0) list->push_back({name, type});
    }

    // パラメータ名コンボボックスを選択エフェクトに応じて更新
    static void PopulateParamCombo(HWND hCombo, const std::wstring& effectName) {
        SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
        if (effectName.empty() || !g_edit_handle) return;

        std::vector<ParamEntry> items;
        g_edit_handle->enum_effect_item(effectName.c_str(), &items, EnumItemCallback);

        for (const auto& it : items) {
            int idx = (int)SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)it.name.c_str());
            if (idx >= 0) SendMessageW(hCombo, CB_SETITEMDATA, idx, (LPARAM)it.type);
        }
        if (SendMessageW(hCombo, CB_GETCOUNT, 0, 0) > 0)
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);
    }
    // 対象コンボボックスを現在のステップ種別に応じて更新
    static void PopulateTargetCombo(HWND hCombo, ActionType stepType) {
        SendMessageW(hCombo, CB_RESETCONTENT, 0, 0);
        if (!g_edit_handle) return;

        std::vector<EffectEntry> effects;
        g_edit_handle->enum_effect_name(&effects, EnumEffectCallback);

        for (const auto& e : effects) {
            bool show = false;
            switch (stepType) {
                case ActionType::DROP_OBJECT:
                    // メディア入力 + オブジェクト制御
                    show = (e.type == 2 || e.type == 4);
                    break;
                case ActionType::ADD_FILTER:
                    // フィルタ効果のみ
                    show = (e.type == 1);
                    break;
                case ActionType::MENU_COMMAND:
                    // enum_effect_name からは表示しない（別途スキャン）
                    break;
                case ActionType::SET_PARAM:
                default:
                    // 全種別
                    show = true;
                    break;
            }
            if (show) SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)e.name.c_str());
        }

        // MENU_COMMAND: AviUtl メニューをスキャン
        if (stepType == ActionType::MENU_COMMAND) {
            std::vector<std::pair<std::wstring, int>> menu_items;
            CommandExecutor::ScanMenuCommands(menu_items);
            for (const auto& mi : menu_items) {
                std::wstring entry = mi.first + L"  [" + std::to_wstring(mi.second) + L"]";
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)entry.c_str());
            }
        }

        if (SendMessageW(hCombo, CB_GETCOUNT, 0, 0) > 0)
            SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

        // DROP_OBJECT: エイリアスファイルも追加
        if (stepType == ActionType::DROP_OBJECT) {
            auto scan_alias_dir = [&](const std::wstring& base_dir) {
                std::wstring pattern = base_dir + L"\\*.object";
                WIN32_FIND_DATAW fd;
                HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            std::wstring entry = L"%Alias%\\" + std::wstring(fd.cFileName);
                            // 重複チェック
                            if (SendMessageW(hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)entry.c_str()) == CB_ERR) {
                                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)entry.c_str());
                            }
                        }
                    } while (FindNextFileW(hFind, &fd));
                    FindClose(hFind);
                }
            };

            // 1) DLL 同階層の alias フォルダ
            if (g_hInstance) {
                WCHAR dll_path[MAX_PATH];
                if (GetModuleFileNameW(g_hInstance, dll_path, MAX_PATH)) {
                    std::wstring dir(dll_path);
                    size_t slash = dir.find_last_of(L"\\/");
                    if (slash != std::wstring::npos) {
                        dir = dir.substr(0, slash) + L"\\alias";
                        scan_alias_dir(dir);
                    }
                }
            }

            // 2) AviUtl2 のアプリケーションデータフォルダ (config->app_data_path)
            if (g_config && g_config->app_data_path) {
                std::wstring dir = std::wstring(g_config->app_data_path) + L"\\alias";
                scan_alias_dir(dir);
            }
        }
    }

    static std::wstring GetComboText(HWND hCombo) {
        WCHAR buf[512] = {0};
        GetWindowTextW(hCombo, buf, 511);
        return buf;
    }

    // コンボボックスの「意味のある」テキストを取得
    // CBN_SELCHANGE 到着時点では編集文字列が未更新（1つ前の選択）の場合があるため、
    // リスト選択があれば CB_GETCURSEL + CB_GETLBTEXT を優先する（CBS_DROPDOWN の既知の罠対策）
    static std::wstring GetComboSelectionText(HWND hCombo) {
        WCHAR buf[256] = {0};
        GetWindowTextW(hCombo, buf, 255);
        if (buf[0] != L'\0') return buf;

        int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
        if (sel >= 0) {
            if (SendMessageW(hCombo, CB_GETLBTEXT, sel, (LPARAM)buf) != CB_ERR) {
                return buf;
            }
        }
        return L"";
    }

    // 選択中のパラメータの型を CB_SETITEMDATA から直接取得
    // enum_effect_item にない項目（セクションレベルパラメータや手入力）は NUMBER(2) 扱い
    static int GetSelectedParamType(HWND hParamCombo, HWND hTargetCombo) {
        int sel = (int)SendMessageW(hParamCombo, CB_GETCURSEL, 0, 0);
        if (sel >= 0) {
            LRESULT type = SendMessageW(hParamCombo, CB_GETITEMDATA, sel, 0);
            if (type != CB_ERR) return (int)type;
        }
        // 選択なし（手入力 / セクションレベルパラメータ）
        return 2;
    }

    // SELECT型パラメータの既知選択肢テーブル
    // SDK に選択肢を列挙する API は存在しないため、プラグイン側で保持する
    // エイリアス実測: 文字揃え=左寄せ[上] のように表示文字列がそのまま設定値となる
    static const wchar_t* g_align_options[] = {
        L"\u5DE6\u5BC4\u305B[\u4E0A]",        // 左寄せ[上]
        L"\u4E2D\u592E\u63C3\u3048[\u4E0A]",  // 中央揃え[上]
        L"\u53F3\u5BC4\u305B[\u4E0A]",        // 右寄せ[上]
        L"\u5DE6\u5BC4\u305B[\u4E2D]",        // 左寄せ[中]
        L"\u4E2D\u592E\u63C3\u3048[\u4E2D]",  // 中央揃え[中]
        L"\u53F3\u5BC4\u305B[\u4E2D]",        // 右寄せ[中]
        L"\u5DE6\u5BC4\u305B[\u4E0B]",        // 左寄せ[下]
        L"\u4E2D\u592E\u63C3\u3048[\u4E0B]",  // 中央揃え[下]
        L"\u53F3\u5BC4\u305B[\u4E0B]",        // 右寄せ[下]
        L"\u7E26\u66F8 \u4E0A\u5BC4[\u53F3]", // 縦書 上寄[右]
        L"\u7E26\u66F8 \u4E2D\u592E[\u53F3]", // 縦書 中央[右]
        L"\u7E26\u66F8 \u4E0B\u5BC4[\u53F3]", // 縦書 下寄[右]
        L"\u7E26\u66F8 \u4E0A\u5BC4[\u4E2D]", // 縦書 上寄[中]
        L"\u7E26\u66F8 \u4E2D\u592E[\u4E2D]", // 縦書 中央[中]
        L"\u7E26\u66F8 \u4E0B\u5BC4[\u4E2D]", // 縦書 下寄[中]
        L"\u7E26\u66F8 \u4E0A\u5BC4[\u5DE6]", // 縦書 上寄[左]
        L"\u7E26\u66F8 \u4E2D\u592E[\u5DE6]", // 縦書 中央[左]
        L"\u7E26\u66F8 \u4E0B\u5BC4[\u5DE6]", // 縦書 下寄[左]
    };

    // パラメータ名から既知のSELECT選択肢を取得（なければ0件）
    // 型判定（enum_effect_item のインデックス整合）に依存せず堅牢に判定する
    static int GetKnownSelectOptions(const std::wstring& paramName, const wchar_t*** outOptions) {
        if (paramName == L"\u6587\u5B57\u63C3\u3048") {  // 文字揃え
            *outOptions = g_align_options;
            return (int)(sizeof(g_align_options) / sizeof(g_align_options[0]));
        }
        *outOptions = nullptr;
        return 0;
    }

    static void UpdateParamValueUI(HWND hDlg, HWND hParamCombo, HWND hTargetCombo) {
        int pType = GetSelectedParamType(hParamCombo, hTargetCombo);
        HWND hEdit = GetDlgItem(hDlg, IDC_EDIT_PARAMVALUE);
        HWND hCheckCombo = GetDlgItem(hDlg, IDC_COMBO_CHECKVAL);
        HWND hFontCombo = GetDlgItem(hDlg, IDC_COMBO_FONTVAL);
        HWND hColorBtn = GetDlgItem(hDlg, IDC_BTN_COLOR);
        HWND hFileBtn = GetDlgItem(hDlg, IDC_BTN_FILE_BROWSE);
        HWND hSelectCombo = GetDlgItem(hDlg, IDC_COMBO_SELECTVAL);
        HWND hModeCombo = GetDlgItem(hDlg, IDC_COMBO_VALMODE);
        HWND hAddModeChk = GetDlgItem(hDlg, IDC_CHK_ADDMODE);

        ShowWindow(hCheckCombo, SW_HIDE);
        ShowWindow(hFontCombo, SW_HIDE);
        ShowWindow(hEdit, SW_HIDE);
        ShowWindow(hColorBtn, SW_HIDE);
        ShowWindow(hFileBtn, SW_HIDE);
        ShowWindow(hSelectCombo, SW_HIDE);
        ShowWindow(hModeCombo, SW_HIDE);
        ShowWindow(hAddModeChk, SW_HIDE);

        std::wstring paramName = GetComboSelectionText(hParamCombo);
        const wchar_t** options = nullptr;
        int optCount = GetKnownSelectOptions(paramName, &options);

        if (pType == 3) {
            ShowWindow(hCheckCombo, SW_SHOW);
            SendMessageW(hCheckCombo, CB_RESETCONTENT, 0, 0);
            SendMessageW(hCheckCombo, CB_ADDSTRING, 0, (LPARAM)L"ON");
            SendMessageW(hCheckCombo, CB_ADDSTRING, 0, (LPARAM)L"OFF");
            SendMessageW(hCheckCombo, CB_SETCURSEL, 0, 0);
        } else if (pType == 13) {
            ShowWindow(hFontCombo, SW_SHOW);
            SendMessageW(hFontCombo, CB_RESETCONTENT, 0, 0);
            if (g_edit_handle) {
                g_edit_handle->enum_font_name(hFontCombo, [](void* p, LPCWSTR name) {
                    if (name && wcslen(name) > 0) SendMessageW((HWND)p, CB_ADDSTRING, 0, (LPARAM)name);
                });
            }
        } else if (pType == 7) {
            ShowWindow(hEdit, SW_SHOW);
            ShowWindow(hColorBtn, SW_SHOW);
            ShowWindow(hAddModeChk, SW_SHOW);
            if (SendMessageW(hAddModeChk, BM_GETCHECK, 0, 0) == BST_CHECKED)
                ShowWindow(hModeCombo, SW_SHOW);
        } else if (pType == 6) {
            ShowWindow(hEdit, SW_SHOW);
            ShowWindow(hFileBtn, SW_SHOW);
            ShowWindow(hAddModeChk, SW_SHOW);
            if (SendMessageW(hAddModeChk, BM_GETCHECK, 0, 0) == BST_CHECKED)
                ShowWindow(hModeCombo, SW_SHOW);
        } else if (optCount > 0) {
            ShowWindow(hSelectCombo, SW_SHOW);
            SendMessageW(hSelectCombo, CB_RESETCONTENT, 0, 0);
            for (int i = 0; i < optCount; ++i) {
                SendMessageW(hSelectCombo, CB_ADDSTRING, 0, (LPARAM)options[i]);
            }
            WCHAR cur[256] = {0};
            GetWindowTextW(hEdit, cur, 255);
            int ci = (int)SendMessageW(hSelectCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)cur);
            if (ci >= 0) SendMessageW(hSelectCombo, CB_SETCURSEL, ci, 0);
        } else {
            ShowWindow(hEdit, SW_SHOW);
            ShowWindow(hAddModeChk, SW_SHOW);
            if (SendMessageW(hAddModeChk, BM_GETCHECK, 0, 0) == BST_CHECKED)
                ShowWindow(hModeCombo, SW_SHOW);
        }
    }

    static void SelectStepTypeInCombo(HWND hCombo, ActionType t) {
        int sel = 0;
        switch (t) {
            case ActionType::DROP_OBJECT:  sel = 0; break;
            case ActionType::ADD_FILTER:   sel = 1; break;
            case ActionType::SET_PARAM:    sel = 2; break;
            case ActionType::MENU_COMMAND:  sel = 3; break;
            case ActionType::COMBO:         sel = 4; break;
        }
        SendMessageW(hCombo, CB_SETCURSEL, (WPARAM)sel, 0);
    }

    static ActionType GetSelectedStepType(HWND hCombo) {
        int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
        switch (sel) {
            case 0: return ActionType::DROP_OBJECT;
            case 1: return ActionType::ADD_FILTER;
            case 2: return ActionType::SET_PARAM;
            case 3: return ActionType::MENU_COMMAND;
            default: return ActionType::COMBO;
        }
    }

    static void CollectStepFromUI(HWND hwnd, ActionStep& st) {
        st.type = GetSelectedStepType(GetDlgItem(hwnd, IDC_COMBO_STEPTYPE));
        st.target_name = GetComboText(GetDlgItem(hwnd, IDC_EDIT_TARGET));
        st.param_name = GetComboText(GetDlgItem(hwnd, IDC_EDIT_PARAMNAME));

        WCHAR buf[512] = {0};
        HWND hValEdit = GetDlgItem(hwnd, IDC_EDIT_PARAMVALUE);
        HWND hValCombo = GetDlgItem(hwnd, IDC_COMBO_CHECKVAL);
        HWND hFontCombo = GetDlgItem(hwnd, IDC_COMBO_FONTVAL);
        HWND hSelCombo = GetDlgItem(hwnd, IDC_COMBO_SELECTVAL);
        if (IsWindowVisible(hValCombo)) {
            wcscpy_s(buf, GetComboText(hValCombo).c_str());
        } else if (IsWindowVisible(hFontCombo)) {
            wcscpy_s(buf, GetComboText(hFontCombo).c_str());
        } else if (IsWindowVisible(hSelCombo)) {
            wcscpy_s(buf, GetComboText(hSelCombo).c_str());
        } else {
            GetWindowTextW(hValEdit, buf, 511);
        }
        st.param_value = buf;

        HWND hAddModeChk = GetDlgItem(hwnd, IDC_CHK_ADDMODE);
        if (IsWindowVisible(hAddModeChk) && SendMessageW(hAddModeChk, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            HWND hModeCombo = GetDlgItem(hwnd, IDC_COMBO_VALMODE);
            int mode = (int)SendMessageW(hModeCombo, CB_GETCURSEL, 0, 0);
            if (mode == 0 && st.param_value[0] != L'+') {
                st.param_value = L"+" + st.param_value;
            } else if (mode == 1 && st.param_value[0] != L'-') {
                st.param_value = L"-" + st.param_value;
            }
        } else if (!st.param_value.empty() && st.param_value[0] == L'-') {
            st.param_value = L"=" + st.param_value;
        }

        if (st.type == ActionType::DROP_OBJECT) {
            int posSel = (int)SendMessageW(GetDlgItem(hwnd, IDC_COMBO_POS), CB_GETCURSEL, 0, 0);
            if (posSel == 2) {
                WCHAR layerBuf[32] = {0};
                GetDlgItemTextW(hwnd, IDC_EDIT_LAYER, layerBuf, 31);
                st.param_value = layerBuf;
            } else {
                st.param_value = (posSel == 1) ? L"seek" : L"mouse";
            }
        }

        if (st.type == ActionType::MENU_COMMAND) {
            size_t lb = st.target_name.find_last_of(L'[');
            size_t rb = st.target_name.find_last_of(L']');
            if (lb != std::wstring::npos && rb != std::wstring::npos && rb > lb) {
                std::wstring id_str = st.target_name.substr(lb + 1, rb - lb - 1);
                st.filter_cmd_id = std::stoi(id_str);
                st.target_name = st.target_name.substr(0, lb);
                while (!st.target_name.empty() && st.target_name.back() == L' ')
                    st.target_name.pop_back();
            }
        }
    }

    INT_PTR CALLBACK CmdEditorDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

        switch (msg) {
            case WM_INITDIALOG: {
                g_setting_hwnd = hwnd;
                CmdEditCtx*                 ctx = (CmdEditCtx*)lp;
                SetWindowLongPtrW(hwnd, DWLP_USER, (LONG_PTR)ctx);
                ctx->hStepList = GetDlgItem(hwnd, IDC_STEP_LIST);
                ctx->hComboType = GetDlgItem(hwnd, IDC_COMBO_STEPTYPE);
                ctx->hEditTarget = GetDlgItem(hwnd, IDC_EDIT_TARGET);
                ctx->hEditParamName = GetDlgItem(hwnd, IDC_EDIT_PARAMNAME);
                ctx->hEditParamValue = GetDlgItem(hwnd, IDC_EDIT_PARAMVALUE);
                ctx->editing_step_idx = -1;

                SetDlgItemTextW(hwnd, IDC_EDIT_NAME, ctx->cmd->name.c_str());
                SetDlgItemTextW(hwnd, IDC_EDIT_CATEGORY, ctx->cmd->category.c_str());

                // ステップタイプコンボ
                HWND hTypeCombo = ctx->hComboType;
                SendMessageW(hTypeCombo, CB_ADDSTRING, 0, (LPARAM)ActionTypeName(ActionType::DROP_OBJECT));
                SendMessageW(hTypeCombo, CB_ADDSTRING, 0, (LPARAM)ActionTypeName(ActionType::ADD_FILTER));
                SendMessageW(hTypeCombo, CB_ADDSTRING, 0, (LPARAM)ActionTypeName(ActionType::SET_PARAM));
                SendMessageW(hTypeCombo, CB_ADDSTRING, 0, (LPARAM)ActionTypeName(ActionType::MENU_COMMAND));
                SendMessageW(hTypeCombo, CB_SETCURSEL, 0, 0);

                // 位置コンボ（投下専用）
                HWND hPosCombo = GetDlgItem(hwnd, IDC_COMBO_POS);
                SendMessageW(hPosCombo, CB_ADDSTRING, 0, (LPARAM)L"マウス位置");
                SendMessageW(hPosCombo, CB_ADDSTRING, 0, (LPARAM)L"シークバー位置");
                SendMessageW(hPosCombo, CB_ADDSTRING, 0, (LPARAM)L"レイヤー指定");
                SendMessageW(hPosCombo, CB_SETCURSEL, 0, 0);

                // 値モードコンボ（加算/減算）
                HWND hValMode = GetDlgItem(hwnd, IDC_COMBO_VALMODE);
                SendMessageW(hValMode, CB_ADDSTRING, 0, (LPARAM)L"+");
                SendMessageW(hValMode, CB_ADDSTRING, 0, (LPARAM)L"-");
                SendMessageW(hValMode, CB_SETCURSEL, 0, 0);

                // ステップリストの列
                {
                    ListView_SetExtendedListViewStyle(ctx->hStepList, LVS_EX_FULLROWSELECT);
                    LVCOLUMNW lvc = { LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM, 0 };
                    RECT rc; GetClientRect(ctx->hStepList, &rc);
                    int w = rc.right - rc.left - 20;
                    lvc.pszText = (LPWSTR)L"種別";       lvc.cx = w * 20 / 100; ListView_InsertColumn(ctx->hStepList, 0, &lvc);
                    lvc.pszText = (LPWSTR)L"対象";       lvc.cx = w * 30 / 100; ListView_InsertColumn(ctx->hStepList, 1, &lvc);
                    lvc.pszText = (LPWSTR)L"パラメータ"; lvc.cx = w * 25 / 100; ListView_InsertColumn(ctx->hStepList, 2, &lvc);
                    lvc.pszText = (LPWSTR)L"値";         lvc.cx = w * 25 / 100; ListView_InsertColumn(ctx->hStepList, 3, &lvc);
                }

                RefreshStepList(ctx);

                // 初期種別で対象コンボをポップアップ、パラメータ欄は非表示
                {
                    ActionType t = GetSelectedStepType(ctx->hComboType);
                    PopulateTargetCombo(ctx->hEditTarget, t);
                    int show = (t == ActionType::SET_PARAM) ? SW_SHOW : SW_HIDE;
                    ShowWindow(GetDlgItem(hwnd, IDC_STATIC_PARAMLABEL), show);
                    ShowWindow(ctx->hEditParamName, show);
                    ShowWindow(GetDlgItem(hwnd, IDC_STATIC_VALUELABEL), show);
                    ShowWindow(ctx->hEditParamValue, show);
                    int showPos = (t == ActionType::DROP_OBJECT) ? SW_SHOW : SW_HIDE;
                    ShowWindow(GetDlgItem(hwnd, IDC_STATIC_POSLABEL), showPos);
                    ShowWindow(GetDlgItem(hwnd, IDC_COMBO_POS), showPos);
                    ShowWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE), showPos);
                    ShowWindow(GetDlgItem(hwnd, IDC_STATIC_LAYERLABEL), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_EDIT_LAYER), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_COMBO_CHECKVAL), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_COMBO_FONTVAL), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_BTN_COLOR), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_BTN_FILE_BROWSE), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_COMBO_VALMODE), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_CHK_ADDMODE), SW_HIDE);
                    ShowWindow(GetDlgItem(hwnd, IDC_COMBO_SELECTVAL), SW_HIDE);
                }

                SetWindowTextW(hwnd, ctx->is_new ? L"\u65B0\u898F\u30B3\u30DE\u30F3\u30C9\u4F5C\u6210" : L"\u30B3\u30DE\u30F3\u30C9\u7DE8\u96C6");
                return TRUE;
            }
            case WM_MEASUREITEM: {
                if ((UINT)wp == IDC_COMBO_FONTVAL) {
                    LPMEASUREITEMSTRUCT mis = (LPMEASUREITEMSTRUCT)lp;
                    mis->itemHeight = 18;
                    return TRUE;
                }
                break;
            }
            case WM_DRAWITEM: {
                if ((UINT)wp == IDC_COMBO_FONTVAL) {
                    LPDRAWITEMSTRUCT dis = (LPDRAWITEMSTRUCT)lp;
                    if (dis->itemID == (UINT)-1) break;
                    WCHAR name[256] = {0};
                    SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)name);
                    if (!name[0]) break;

                    COLORREF oldText = SetTextColor(dis->hDC,
                        (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHTTEXT) : GetSysColor(COLOR_WINDOWTEXT));
                    COLORREF oldBk = SetBkColor(dis->hDC,
                        (dis->itemState & ODS_SELECTED) ? GetSysColor(COLOR_HIGHLIGHT) : GetSysColor(COLOR_WINDOW));
                    ExtTextOutW(dis->hDC, 0, 0, ETO_OPAQUE, &dis->rcItem, L"", 0, nullptr);

                    HFONT hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, DEFAULT_PITCH, name);
                    if (hFont) {
                        HFONT oldFont = (HFONT)SelectObject(dis->hDC, hFont);
                        RECT rc = dis->rcItem;
                        rc.left += 2;
                        DrawTextW(dis->hDC, name, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                        SelectObject(dis->hDC, oldFont);
                        DeleteObject(hFont);
                    } else {
                        RECT rc = dis->rcItem;
                        rc.left += 2;
                        DrawTextW(dis->hDC, name, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    }

                    SetTextColor(dis->hDC, oldText);
                    SetBkColor(dis->hDC, oldBk);
                    if (dis->itemState & ODS_FOCUS) DrawFocusRect(dis->hDC, &dis->rcItem);
                    return TRUE;
                }
                break;
            }
            case WM_NOTIFY: {
                CmdEditCtx* ctx = (CmdEditCtx*)GetWindowLongPtrW(hwnd, DWLP_USER);
                if (!ctx) break;
                NMHDR* nm = (NMHDR*)lp;
                if (nm->idFrom == IDC_STEP_LIST && nm->code == LVN_ITEMCHANGED) {
                    NMLISTVIEW* nmlv = (NMLISTVIEW*)lp;
                    if ((nmlv->uNewState & LVIS_SELECTED) && nmlv->iItem >= 0 && nmlv->iItem < (int)ctx->cmd->steps.size()) {
                        // 選択されたステップの内容を編集欄にロード
                        const ActionStep& st = ctx->cmd->steps[nmlv->iItem];
                        ctx->editing_step_idx = nmlv->iItem;
                        SelectStepTypeInCombo(ctx->hComboType, st.type);
                        PopulateTargetCombo(ctx->hEditTarget, st.type);
                        // 対象を選択（なければ追加）
                        int idx = (int)SendMessageW(ctx->hEditTarget, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)st.target_name.c_str());
                        if (idx >= 0) {
                            SendMessageW(ctx->hEditTarget, CB_SETCURSEL, idx, 0);
                        } else if (!st.target_name.empty()) {
                            SendMessageW(ctx->hEditTarget, CB_ADDSTRING, 0, (LPARAM)st.target_name.c_str());
                            SendMessageW(ctx->hEditTarget, CB_SETCURSEL, (WPARAM)SendMessageW(ctx->hEditTarget, CB_GETCOUNT, 0, 0) - 1, 0);
                        }
                        // パラメータ名
                        if (st.type == ActionType::SET_PARAM && !st.target_name.empty()) {
                            PopulateParamCombo(ctx->hEditParamName, st.target_name);
                            idx = (int)SendMessageW(ctx->hEditParamName, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)st.param_name.c_str());
                            if (idx >= 0) {
                                SendMessageW(ctx->hEditParamName, CB_SETCURSEL, idx, 0);
                            } else if (!st.param_name.empty()) {
                                // enum_effect_item にない項目（セクションレベル等）はテキスト直接設定
                                SetWindowTextW(ctx->hEditParamName, st.param_name.c_str());
                            }
                        }
                        SetDlgItemTextW(hwnd, IDC_EDIT_PARAMVALUE, st.param_value.c_str());

                        // 加減算チェックボックスとドロップダウンを復元
                        {
                            HWND hChk = GetDlgItem(hwnd, IDC_CHK_ADDMODE);
                            HWND hMode = GetDlgItem(hwnd, IDC_COMBO_VALMODE);
                            if (!st.param_value.empty() && (st.param_value[0] == L'+' || st.param_value[0] == L'-')) {
                                SendMessageW(hChk, BM_SETCHECK, BST_CHECKED, 0);
                                int modeSel = (st.param_value[0] == L'+') ? 0 : 1;
                                SendMessageW(hMode, CB_SETCURSEL, modeSel, 0);
                                ShowWindow(hMode, SW_SHOW);
                                SetDlgItemTextW(hwnd, IDC_EDIT_PARAMVALUE, st.param_value.substr(1).c_str());
                            } else {
                                SendMessageW(hChk, BM_SETCHECK, BST_UNCHECKED, 0);
                                ShowWindow(hMode, SW_HIDE);
                                if (!st.param_value.empty() && st.param_value[0] == L'=') {
                                    SetDlgItemTextW(hwnd, IDC_EDIT_PARAMVALUE, st.param_value.substr(1).c_str());
                                }
                            }
                        }

                        // 値入力UIをパラメータ型に応じて切替
                        if (st.type == ActionType::SET_PARAM) {
                            UpdateParamValueUI(hwnd, ctx->hEditParamName, ctx->hEditTarget);
                        }

                        // CHECK型なら値コンボを反映
                        if (st.type == ActionType::SET_PARAM) {
                            int pType = GetSelectedParamType(ctx->hEditParamName, ctx->hEditTarget);
                            if (pType == 3) {
                                HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_CHECKVAL);
                                int ci = (int)SendMessageW(hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)st.param_value.c_str());
                                if (ci >= 0) SendMessageW(hCombo, CB_SETCURSEL, ci, 0);
                            } else if (pType == 13) {
                                HWND hCombo = GetDlgItem(hwnd, IDC_COMBO_FONTVAL);
                                int ci = (int)SendMessageW(hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)st.param_value.c_str());
                                if (ci >= 0) SendMessageW(hCombo, CB_SETCURSEL, ci, 0);
                            }
                        }

                        // 位置コンボ復元
                        if (st.type == ActionType::DROP_OBJECT) {
                            HWND hPos = GetDlgItem(hwnd, IDC_COMBO_POS);
                            int posIdx = 0;
                            if (st.param_value == L"seek") posIdx = 1;
                            else if (!st.param_value.empty() && st.param_value != L"mouse") posIdx = 2;
                            SendMessageW(hPos, CB_SETCURSEL, posIdx, 0);
                            int showLayer = (posIdx == 2) ? SW_SHOW : SW_HIDE;
                            ShowWindow(GetDlgItem(hwnd, IDC_STATIC_LAYERLABEL), showLayer);
                            ShowWindow(GetDlgItem(hwnd, IDC_EDIT_LAYER), showLayer);
                            if (posIdx == 2) SetDlgItemTextW(hwnd, IDC_EDIT_LAYER, st.param_value.c_str());
                        }

                        int show = (st.type == ActionType::SET_PARAM) ? SW_SHOW : SW_HIDE;
                        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_PARAMLABEL), show);
                        ShowWindow(ctx->hEditParamName, show);
                        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_VALUELABEL), show);
                        ShowWindow(ctx->hEditParamValue, show);
                    }
                }
                break;
            }
            case WM_COMMAND: {
                CmdEditCtx* ctx = (CmdEditCtx*)GetWindowLongPtrW(hwnd, DWLP_USER);
                if (!ctx) break;
                WORD id = LOWORD(wp);
                WORD code = HIWORD(wp);

                // コンボボックス選択変更 / 編集変更
                if (code == CBN_SELCHANGE) {
                    if (id == IDC_COMBO_STEPTYPE) {
                        ActionType t = GetSelectedStepType(ctx->hComboType);
                        SendMessageW(ctx->hEditTarget, CB_RESETCONTENT, 0, 0);
                        SendMessageW(ctx->hEditParamName, CB_RESETCONTENT, 0, 0);
                        PopulateTargetCombo(ctx->hEditTarget, t);

                        // パラメータ名/値は SET_PARAM の時のみ表示
                        int show = (t == ActionType::SET_PARAM) ? SW_SHOW : SW_HIDE;
                        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_PARAMLABEL), show);
                        ShowWindow(ctx->hEditParamName, show);
                        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_VALUELABEL), show);
                        ShowWindow(ctx->hEditParamValue, show);
                        // 位置は投下のみ表示
                        int showPos = (t == ActionType::DROP_OBJECT) ? SW_SHOW : SW_HIDE;
                        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_POSLABEL), showPos);
                        ShowWindow(GetDlgItem(hwnd, IDC_COMBO_POS), showPos);
                        ShowWindow(GetDlgItem(hwnd, IDC_BTN_BROWSE), showPos);
                        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_LAYERLABEL), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_LAYER), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_COMBO_CHECKVAL), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_COMBO_FONTVAL), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_BTN_COLOR), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_BTN_FILE_BROWSE), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_COMBO_VALMODE), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_CHK_ADDMODE), SW_HIDE);
                        ShowWindow(GetDlgItem(hwnd, IDC_COMBO_SELECTVAL), SW_HIDE);
                        if (t == ActionType::SET_PARAM) {
                            // 初期ターゲットのパラメータ名を列挙
                            std::wstring eff = GetComboSelectionText(ctx->hEditTarget);
                            if (!eff.empty()) PopulateParamCombo(ctx->hEditParamName, eff);
                            // 種別切替で値コンボの選択肢が残留しないよう更新
                            UpdateParamValueUI(hwnd, ctx->hEditParamName, ctx->hEditTarget);
                        }
                        return TRUE;
                    }
                    if (id == IDC_EDIT_TARGET) {
                        ActionType t = GetSelectedStepType(ctx->hComboType);
                        if (t == ActionType::SET_PARAM) {
                            std::wstring eff = GetComboSelectionText(ctx->hEditTarget);
                            SendMessageW(ctx->hEditParamName, CB_RESETCONTENT, 0, 0);
                            if (!eff.empty()) PopulateParamCombo(ctx->hEditParamName, eff);
                            // 対象変更で値コンボの選択肢が残留しないよう更新
                            UpdateParamValueUI(hwnd, ctx->hEditParamName, ctx->hEditTarget);
                        }
                        return TRUE;
                    }
                    if (id == IDC_COMBO_POS) {
                        int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_COMBO_POS), CB_GETCURSEL, 0, 0);
                        int showLayer = (sel == 2) ? SW_SHOW : SW_HIDE;
                        ShowWindow(GetDlgItem(hwnd, IDC_STATIC_LAYERLABEL), showLayer);
                        ShowWindow(GetDlgItem(hwnd, IDC_EDIT_LAYER), showLayer);
                        return TRUE;
                    }
                    if (id == IDC_EDIT_PARAMNAME) {
                        UpdateParamValueUI(hwnd, ctx->hEditParamName, ctx->hEditTarget);
                        return TRUE;
                    }
                }

                // パラメータ名の手入力（CBS_DROPDOWN の編集）にも対応
                if (code == CBN_EDITCHANGE && id == IDC_EDIT_PARAMNAME) {
                    UpdateParamValueUI(hwnd, ctx->hEditParamName, ctx->hEditTarget);
                    return TRUE;
                }

                // リスト選択確定時にも最終状態を保証（CBN_SELCHANGE のテキスト未更新問題を吸収）
                if (code == CBN_SELENDOK && id == IDC_EDIT_PARAMNAME) {
                    UpdateParamValueUI(hwnd, ctx->hEditParamName, ctx->hEditTarget);
                    return TRUE;
                }

                switch (id) {
                    case IDC_BTN_BROWSE: {
                        WCHAR fileBuf[MAX_PATH * 2] = {0};
                        OPENFILENAMEW ofn = { sizeof(ofn) };
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = L"メディアファイル\0*.*\0";
                        ofn.lpstrFile = fileBuf;
                        ofn.nMaxFile = MAX_PATH * 2;
                        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
                        if (GetOpenFileNameW(&ofn)) {
                            SendMessageW(ctx->hEditTarget, CB_ADDSTRING, 0, (LPARAM)fileBuf);
                            int idx = (int)SendMessageW(ctx->hEditTarget, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)fileBuf);
                            if (idx >= 0) SendMessageW(ctx->hEditTarget, CB_SETCURSEL, idx, 0);
                        }
                        break;
                    }
                    case IDC_BTN_COLOR: {
                        static COLORREF g_cust[16] = {};
                        CHOOSECOLORW cc = { sizeof(cc) };
                        cc.hwndOwner = hwnd;
                        cc.lpCustColors = g_cust;
                        cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                        // 現在の値から色を復元
                        WCHAR cur[32];
                        GetDlgItemTextW(hwnd, IDC_EDIT_PARAMVALUE, cur, 31);
                        cc.rgbResult = wcstoul(cur, nullptr, 16);
                        if (ChooseColorW(&cc)) {
                            WCHAR hex[16];
                            swprintf_s(hex, L"%06x", cc.rgbResult & 0xFFFFFF);
                            SetDlgItemTextW(hwnd, IDC_EDIT_PARAMVALUE, hex);
                        }
                        break;
                    }
                    case IDC_BTN_FILE_BROWSE: {
                        WCHAR fileBuf[MAX_PATH * 2] = {0};
                        // 現在の値から初期パスを復元
                        WCHAR cur[MAX_PATH];
                        GetDlgItemTextW(hwnd, IDC_EDIT_PARAMVALUE, cur, MAX_PATH);
                        if (cur[0]) {
                            wcscpy_s(fileBuf, cur);
                        }
                        OPENFILENAMEW ofn = { sizeof(ofn) };
                        ofn.hwndOwner = hwnd;
                        ofn.lpstrFilter = L"メディアファイル\0*.*\0";
                        ofn.lpstrFile = fileBuf;
                        ofn.nMaxFile = MAX_PATH * 2;
                        ofn.Flags = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
                        if (GetOpenFileNameW(&ofn)) {
                            SetDlgItemTextW(hwnd, IDC_EDIT_PARAMVALUE, fileBuf);
                        }
                        break;
                    }
                    case IDC_CHK_ADDMODE: {
                        HWND hModeCombo = GetDlgItem(hwnd, IDC_COMBO_VALMODE);
                        if (SendMessageW((HWND)lp, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                            ShowWindow(hModeCombo, SW_SHOW);
                        } else {
                            ShowWindow(hModeCombo, SW_HIDE);
                        }
                        break;
                    }
                    case IDC_BTN_ADD_STEP: {
                        ActionStep st;
                        CollectStepFromUI(hwnd, st);
                        ctx->cmd->steps.push_back(st);
                        RefreshStepList(ctx);
                        break;
                    }
                    case IDC_BTN_UPDATE: {
                        int sel = ListView_GetNextItem(ctx->hStepList, -1, LVNI_SELECTED);
                        if (sel >= 0 && sel < (int)ctx->cmd->steps.size()) {
                            ActionStep& st = ctx->cmd->steps[sel];
                            CollectStepFromUI(hwnd, st);
                            ctx->editing_step_idx = -1;
                            RefreshStepList(ctx);
                        }
                        break;
                    }
                    case IDC_BTN_DEL_STEP: {
                        int sel = ListView_GetNextItem(ctx->hStepList, -1, LVNI_SELECTED);
                        if (sel >= 0 && sel < (int)ctx->cmd->steps.size()) {
                            ctx->cmd->steps.erase(ctx->cmd->steps.begin() + sel);
                            ctx->editing_step_idx = -1;
                            RefreshStepList(ctx);
                        } else {
                            MessageBoxW(hwnd, L"削除するステップを一覧から選択してください。", L"Any_Shortcut_for_AviUtl2", MB_ICONINFORMATION);
                        }
                        break;
                    }
                    case IDC_BTN_UP: {
                        int sel = ListView_GetNextItem(ctx->hStepList, -1, LVNI_SELECTED);
                        if (sel > 0 && sel < (int)ctx->cmd->steps.size()) {
                            std::swap(ctx->cmd->steps[sel], ctx->cmd->steps[sel - 1]);
                            RefreshStepList(ctx);
                            ListView_SetItemState(ctx->hStepList, sel - 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        }
                        break;
                    }
                    case IDC_BTN_DOWN: {
                        int sel = ListView_GetNextItem(ctx->hStepList, -1, LVNI_SELECTED);
                        if (sel >= 0 && sel < (int)ctx->cmd->steps.size() - 1) {
                            std::swap(ctx->cmd->steps[sel], ctx->cmd->steps[sel + 1]);
                            RefreshStepList(ctx);
                            ListView_SetItemState(ctx->hStepList, sel + 1, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
                        }
                        break;
                    }
                    case IDC_BTN_OK: {
                        WCHAR buf[512];
                        GetDlgItemTextW(hwnd, IDC_EDIT_NAME, buf, 512);
                        ctx->cmd->name = buf;
                        GetDlgItemTextW(hwnd, IDC_EDIT_CATEGORY, buf, 512);
                        ctx->cmd->category = buf;
                        ctx->cmd->virtual_key = 0;

                        if (ctx->cmd->name.empty()) {
                            MessageBoxW(hwnd, L"\u540D\u524D\u3092\u5165\u529B\u3057\u3066\u304F\u3060\u3055\u3044\u3002", L"Any_Shortcut_for_AviUtl2", MB_ICONWARNING);
                            return TRUE;
                        }
                        EndDialog(hwnd, IDOK);
                        return TRUE;
                    }
                    case IDC_BTN_CANCEL:
                        EndDialog(hwnd, IDCANCEL);
                        return TRUE;
                }
                break;
            }
            case WM_CLOSE:
                g_setting_hwnd = nullptr;
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
        }
        return FALSE;
    }

    // コマンド編集を開く
    bool OpenCommandEditor(HWND parent, ShortcutCommand* cmd) {
        CmdEditCtx ctx;
        ShortcutCommand tmp_cmd;
        bool is_new = (cmd == nullptr);
        if (is_new) {
            tmp_cmd.id = ConfigManager::GenerateNewCommandId();
            tmp_cmd.name = L"";
            tmp_cmd.category = L"";
            tmp_cmd.virtual_key = 0;
            cmd = &tmp_cmd;
        } else {
            tmp_cmd = *cmd;
            cmd = &tmp_cmd;
        }

        ctx.cmd = cmd;
        ctx.is_new = is_new;

        INT_PTR result = DialogBoxParamW(g_hInstance, MAKEINTRESOURCEW(IDD_CMDEDIT_DIALOG), parent, CmdEditorDlgProc, (LPARAM)&ctx);
        if (result == IDOK) {
            if (is_new) {
                g_commands.push_back(*cmd);
            } else if (g_editing_cmd) {
                *g_editing_cmd = *cmd;
            }
            ConfigManager::SaveConfig();
            return true;
        }
        return false;
    }

    // メインダイアログプロシージャ
    INT_PTR CALLBACK DlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_INITDIALOG: {
                g_setting_hwnd = hwnd;
                g_hMainDlg = hwnd;
                HWND hList = GetDlgItem(hwnd, IDC_CMD_LIST);

                ListView_SetExtendedListViewStyle(hList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
                InitListViewColumns(hList);
                RefreshCommandListView(hList);
                return TRUE;
            }
            case WM_COMMAND: {
                WORD id = LOWORD(wp);
                switch (id) {
                    case IDC_BTN_ADD: {
                        if (OpenCommandEditor(hwnd, nullptr)) {
                            RefreshCommandListView(GetDlgItem(hwnd, IDC_CMD_LIST));
                            ProposeRestartForNewCommand();
                        }
                        return TRUE;
                    }
                    case IDC_BTN_EDIT: {
                        HWND hList = GetDlgItem(hwnd, IDC_CMD_LIST);
                        int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                        if (sel >= 0 && sel < (int)g_commands.size()) {
                            g_editing_cmd = &g_commands[sel];
                            if (OpenCommandEditor(hwnd, g_editing_cmd)) {
                                RefreshCommandListView(hList);
                            }
                            g_editing_cmd = nullptr;
                        }
                        return TRUE;
                    }
                    case IDC_BTN_DELETE: {
                        HWND hList = GetDlgItem(hwnd, IDC_CMD_LIST);
                        int sel = ListView_GetNextItem(hList, -1, LVNI_SELECTED);
                        if (sel >= 0 && sel < (int)g_commands.size()) {
                            std::wstring msg = L"\u30B3\u30DE\u30F3\u30C9\u300C" + g_commands[sel].name + L"\u300D\u3092\u524A\u9664\u3057\u307E\u3059\u304B\uFF1F";
                            if (MessageBoxW(hwnd, msg.c_str(), L"Any_Shortcut_for_AviUtl2", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                                g_commands.erase(g_commands.begin() + sel);
                                ConfigManager::SaveConfig();
                                RefreshCommandListView(hList);
                                ProposeRestartForNewCommand();
                            }
                        }
                        return TRUE;
                    }
                    case IDC_BTN_SAVE: {
                        ConfigManager::SaveConfig();
                        MessageBoxW(hwnd, L"\u4FDD\u5B58\u3057\u307E\u3057\u305F\u3002", L"Any_Shortcut_for_AviUtl2", MB_ICONINFORMATION);
                        return TRUE;
                    }
                    case IDC_BTN_CLOSE:
                        EndDialog(hwnd, 0);
                        return TRUE;
                }
                break;
            }
            case WM_CLOSE:
                g_setting_hwnd = nullptr;
                EndDialog(hwnd, 0);
                return TRUE;
        }
        return FALSE;
    }

    void ShowSettingWindow(HWND parent, HINSTANCE hinst) {
        // コモンコントロール初期化
        INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);

        DialogBoxParamW(hinst, MAKEINTRESOURCEW(IDD_MAIN_DIALOG), parent, DlgProc, 0);
    }

    void ProposeRestartForNewCommand() {
        ConfigManager::SaveConfig();
        int res = MessageBoxW(
            g_edit_handle ? g_edit_handle->get_host_app_window() : nullptr,
            L"コマンドを保存しました。\n\nショートカットキー設定への反映にはAviUtlの再起動が必要です。\n今すぐ再起動しますか？",
            L"Any_Shortcut_for_AviUtl2",
            MB_YESNO | MB_ICONQUESTION
        );
        if (res == IDYES && g_edit_handle) {
            g_edit_handle->restart_host_app();
        }
    }
}
