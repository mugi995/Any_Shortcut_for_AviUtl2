#include "ConfigManager.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ConfigManager {

    std::wstring GetDllFolderPath() {
        WCHAR mod_path[MAX_PATH] = {0};
        if (g_hInstance && GetModuleFileNameW(g_hInstance, mod_path, MAX_PATH)) {
            std::wstring full(mod_path);
            size_t slash = full.find_last_of(L"\\/");
            if (slash != std::wstring::npos) return full.substr(0, slash);
        }
        return L".";
    }

    std::wstring GetConfigFilePath() {
        return GetDllFolderPath() + L"\\Any_Shortcut_for_AviUtl2.json";
    }

    int GenerateNewCommandId() {
        int max_id = 1000;
        for (const auto& cmd : g_commands)
            if (cmd.id > max_id) max_id = cmd.id;
        return max_id + 1;
    }

    void CreateDefaultCommands() {
        g_commands.clear();
    }

    static std::wstring EscapeJson(const std::wstring& str) {
        std::wstring out;
        for (wchar_t c : str) {
            switch (c) {
                case L'\\': out += L"\\\\"; break;
                case L'\"': out += L"\\\""; break;
                case L'\n': out += L"\\n";  break;
                case L'\r': out += L"\\r";  break;
                case L'\t': out += L"\\t";  break;
                default:    out += c;       break;
            }
        }
        return out;
    }

    bool SaveConfig() {
        std::wstring path = GetConfigFilePath();
        std::wstring tmp_path = path + L".tmp";

        std::ofstream ofs(tmp_path, std::ios::binary);
        if (!ofs.is_open()) return false;

        std::wstringstream ss;
        ss << L"{\n  \"version\": 1,\n  \"commands\": [\n";
        for (size_t i = 0; i < g_commands.size(); ++i) {
            const auto& cmd = g_commands[i];
            ss << L"    {\n";
            ss << L"      \"id\": " << cmd.id << L",\n";
            ss << L"      \"name\": \"" << EscapeJson(cmd.name) << L"\",\n";
            ss << L"      \"category\": \"" << EscapeJson(cmd.category) << L"\",\n";
            ss << L"      \"virtual_key\": " << cmd.virtual_key << L",\n";
            ss << L"      \"modifiers\": " << cmd.modifiers << L",\n";
            ss << L"      \"steps\": [\n";
            for (size_t j = 0; j < cmd.steps.size(); ++j) {
                const auto& st = cmd.steps[j];
                ss << L"        {\n";
                ss << L"          \"type\": " << (int)st.type << L",\n";
                ss << L"          \"target_name\": \"" << EscapeJson(st.target_name) << L"\",\n";
                ss << L"          \"param_name\": \"" << EscapeJson(st.param_name) << L"\",\n";
                ss << L"          \"param_value\": \"" << EscapeJson(st.param_value) << L"\",\n";
                ss << L"          \"filter_cmd_id\": " << st.filter_cmd_id << L"\n";
                ss << L"        }" << (j + 1 < cmd.steps.size() ? L"," : L"") << L"\n";
            }
            ss << L"      ]\n";
            ss << L"    }" << (i + 1 < g_commands.size() ? L"," : L"") << L"\n";
        }
        ss << L"  ]\n}\n";

        std::string utf8 = WStringToString(ss.str());
        ofs.write(utf8.c_str(), utf8.size());
        ofs.close();

        if (ofs.fail()) {
            DeleteFileW(tmp_path.c_str());
            return false;
        }

        if (!MoveFileExW(tmp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(tmp_path.c_str());
            return false;
        }
        return true;
    }

    static std::wstring ExtractStr(const std::wstring& block, const std::wstring& key) {
        size_t pos = block.find(L"\"" + key + L"\"");
        if (pos == std::wstring::npos) return L"";
        pos = block.find(L"\"", block.find(L":", pos) + 1);
        if (pos == std::wstring::npos) return L"";
        size_t end = block.find(L"\"", pos + 1);
        while (end != std::wstring::npos) {
            int bs = 0;
            for (size_t j = end; j > 0 && block[j - 1] == L'\\'; j--) bs++;
            if (bs % 2 == 0) break;
            end = block.find(L"\"", end + 1);
        }
        if (end == std::wstring::npos) return L"";
        std::wstring val = block.substr(pos + 1, end - pos - 1);
        std::wstring res;
        for (size_t i = 0; i < val.size(); ++i) {
            if (val[i] == L'\\' && i + 1 < val.size()) {
                switch (val[i + 1]) {
                    case L'\\': res += L'\\'; break;
                    case L'\"': res += L'\"'; break;
                    case L'n':  res += L'\n'; break;
                    case L'r':  res += L'\r'; break;
                    case L't':  res += L'\t'; break;
                    default:    res += val[i]; continue;
                }
                i++;
            } else {
                res += val[i];
            }
        }
        return res;
    }

    static int ExtractInt(const std::wstring& block, const std::wstring& key, int def = 0) {
        size_t pos = block.find(L"\"" + key + L"\"");
        if (pos == std::wstring::npos) return def;
        pos = block.find(L":", pos);
        if (pos == std::wstring::npos) return def;
        pos++;
        while (pos < block.size() && (block[pos] == L' ' || block[pos] == L'\t' || block[pos] == L'\r' || block[pos] == L'\n')) pos++;
        try { return std::stoi(block.substr(pos)); }
        catch (...) { return def; }
    }

    bool LoadConfig() {
        std::wstring path = GetDllFolderPath() + L"\\Any_Shortcut_for_AviUtl2.json";
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs.is_open()) {
            CreateDefaultCommands();
            SaveConfig();
            return true;
        }

        std::string utf8((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();
        std::wstring content = StringToWString(utf8);

        if (content.find(L"\"version\"") == std::wstring::npos) {
            CreateDefaultCommands();
            SaveConfig();
            return true;
        }

        g_commands.clear();
        size_t pos = 0;
        while ((pos = content.find(L"\"id\"", pos)) != std::wstring::npos) {
            // この "id" を含むオブジェクトの開始位置を探す
            size_t obj_start = content.rfind(L'{', pos);
            if (obj_start == std::wstring::npos) { pos++; continue; }

            // 対応する閉じ括弧をカウントで特定（文字列リテラル内の括弧は無視）
            int depth = 0;
            bool in_string = false;
            size_t obj_end = obj_start;
            for (size_t i = obj_start; i < content.size(); i++) {
                if (content[i] == L'"' && (i == 0 || content[i-1] != L'\\')) {
                    in_string = !in_string;
                    continue;
                }
                if (in_string) continue;
                if (content[i] == L'{') depth++;
                else if (content[i] == L'}') {
                    depth--;
                    if (depth == 0) { obj_end = i; break; }
                }
            }
            if (depth != 0) break; // 括弧が一致しない

            std::wstring block = content.substr(obj_start, obj_end - obj_start + 1);
            ShortcutCommand cmd;
            cmd.id = ExtractInt(block, L"id", 1000 + (int)g_commands.size());
            cmd.name = ExtractStr(block, L"name");
            cmd.category = ExtractStr(block, L"category");
            cmd.virtual_key = ExtractInt(block, L"virtual_key", 0);
            cmd.modifiers = ExtractInt(block, L"modifiers", 0);

            size_t steps_pos = block.find(L"\"steps\"");
            if (steps_pos != std::wstring::npos) {
                size_t arr_start = block.find(L"[", steps_pos);
                if (arr_start != std::wstring::npos) {
                    // steps配列の閉じ括弧をカウントで特定（文字列リテラル内の括弧は無視）
                    int arr_depth = 0;
                    bool arr_in_string = false;
                    size_t arr_end = arr_start;
                    for (size_t i = arr_start; i < block.size(); i++) {
                        if (block[i] == L'"' && (i == 0 || block[i-1] != L'\\')) {
                            arr_in_string = !arr_in_string;
                            continue;
                        }
                        if (arr_in_string) continue;
                        if (block[i] == L'[') arr_depth++;
                        else if (block[i] == L']') {
                            arr_depth--;
                            if (arr_depth == 0) { arr_end = i; break; }
                        }
                    }
                    std::wstring steps_str = block.substr(arr_start, arr_end - arr_start);
                    size_t sp = 0;
                    while ((sp = steps_str.find(L"\"type\"", sp)) != std::wstring::npos) {
                        size_t st_start = steps_str.rfind(L'{', sp);
                        if (st_start == std::wstring::npos) { sp++; continue; }
                        int st_depth = 0;
                        bool st_in_string = false;
                        size_t ep = st_start;
                        for (size_t i = st_start; i < steps_str.size(); i++) {
                            if (steps_str[i] == L'"' && (i == 0 || steps_str[i-1] != L'\\')) {
                                st_in_string = !st_in_string;
                                continue;
                            }
                            if (st_in_string) continue;
                            if (steps_str[i] == L'{') st_depth++;
                            else if (steps_str[i] == L'}') {
                                st_depth--;
                                if (st_depth == 0) { ep = i; break; }
                            }
                        }
                        std::wstring st_blk = steps_str.substr(st_start, ep - st_start + 1);
                        ActionStep st;
                        st.type = (ActionType)ExtractInt(st_blk, L"type", 1);
                        st.target_name = ExtractStr(st_blk, L"target_name");
                        st.param_name = ExtractStr(st_blk, L"param_name");
                        st.param_value = ExtractStr(st_blk, L"param_value");
                        st.filter_cmd_id = ExtractInt(st_blk, L"filter_cmd_id", 0);
                        cmd.steps.push_back(st);
                        sp = ep + 1;
                    }
                }
            }
            pos = obj_end + 1;
            if (!cmd.name.empty()) g_commands.push_back(cmd);
        }

        if (g_commands.empty()) {
            std::wstring bak_path = path + L".bak";
            CopyFileW(path.c_str(), bak_path.c_str(), FALSE);
            CreateDefaultCommands();
            SaveConfig();
        }
        return true;
    }

    ShortcutCommand* FindCommandById(int id) {
        for (auto& cmd : g_commands)
            if (cmd.id == id) return &cmd;
        return nullptr;
    }
}
