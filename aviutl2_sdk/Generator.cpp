#include "Generator.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <set>
#include <algorithm>
#include <windows.h>
#include <cmath>

std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstrTo[0], size_needed);
    return wstrTo;
}

std::string wstring_to_utf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::wstring hex_to_wstring(const std::string& hex) {
    std::string bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        char byte = (char)strtol(byteString.c_str(), NULL, 16);
        bytes.push_back(byte);
    }
    std::wstring result;
    for (size_t i = 0; i < bytes.length(); i += 2) {
        wchar_t w = (bytes[i] & 0xFF) | ((bytes[i+1] & 0xFF) << 8);
        if (w == 0) break;
        result.push_back(w);
    }
    return result;
}

struct Subtitle {
    int start_f, end_f;
    std::wstring text;
    std::wstring char_name;
};

std::wstring replace_all(std::wstring str, const std::wstring& from, const std::wstring& to) {
    if(from.empty()) return str;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::wstring::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
    return str;
}

std::string format_time(int frames, double fps) {
    double sec = frames / fps;
    int h = (int)(sec / 3600);
    int m = (int)(fmod(sec, 3600) / 60);
    int s = (int)fmod(sec, 60);
    int ms = (int)round((sec - floor(sec)) * 1000);
    if(ms == 1000) { ms = 0; s++; }
    if(s == 60) { s = 0; m++; }
    if(m == 60) { m = 0; h++; }
    char buf[128];
    sprintf_s(buf, "%02d:%02d:%02d,%03d", h, m, s, ms);
    return buf;
}

bool GenerateSrt(const std::wstring& aup2_path, const std::wstring& output_srt, const Config& config) {
    std::ifstream file(aup2_path);
    if (!file.is_open()) return false;

    std::string line;
    int rate = 60;
    int scale = 1;

    struct Obj {
        int id = 0;
        std::vector<std::string> effects;
        int layer = -1;
        std::wstring char_id;
        std::wstring text;
        std::wstring frame_str;
    };

    std::vector<Obj> objects;
    Obj current_obj;
    bool in_obj = false;

    std::regex re_obj(R"(^\[(\d+)\]$)");
    std::regex re_float(R"(^\[\d+\.\d+\]$)");

    std::string prefix_char = (const char*)u8"キャラクターID=";
    std::string prefix_text = (const char*)u8"テキスト=";

    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.rfind("video.rate=", 0) == 0) {
            rate = std::stoi(line.substr(11));
        } else if (line.rfind("video.scale=", 0) == 0) {
            scale = std::stoi(line.substr(12));
        }

        std::smatch m;
        if (std::regex_match(line, m, re_obj)) {
            if (in_obj && !current_obj.text.empty()) {
                objects.push_back(current_obj);
            }
            current_obj = Obj();
            current_obj.id = std::stoi(m[1].str());
            in_obj = true;
            continue;
        }

        if (std::regex_match(line, m, re_float)) continue;

        if (in_obj) {
            if (line.rfind("frame=", 0) == 0) {
                current_obj.frame_str = utf8_to_wstring(line.substr(6));
            } else if (line.rfind("layer=", 0) == 0) {
                current_obj.layer = std::stoi(line.substr(6));
            } else if (line.rfind("effect.name=", 0) == 0) {
                current_obj.effects.push_back(line.substr(12));
            } else if (line.rfind(prefix_char, 0) == 0) {
                current_obj.char_id = utf8_to_wstring(line.substr(prefix_char.length()));
            } else if (line.rfind(prefix_text, 0) == 0) {
                current_obj.text = utf8_to_wstring(line.substr(prefix_text.length()));
            } else if (line.rfind("text=", 0) == 0) {
                current_obj.text = hex_to_wstring(line.substr(5));
            }
        }
    }
    if (in_obj && !current_obj.text.empty()) {
        objects.push_back(current_obj);
    }

    double fps = (double)rate / scale;

    std::map<int, std::wstring> layer_to_char_id;
    for (const auto& obj : objects) {
        if (obj.text.rfind(L"<?", 0) == 0) {
            size_t id_pos = obj.text.find(L"id=\"");
            if (id_pos != std::wstring::npos) {
                size_t end_pos = obj.text.find(L"\"", id_pos + 4);
                if (end_pos != std::wstring::npos && obj.layer != -1) {
                    layer_to_char_id[obj.layer - 1] = obj.text.substr(id_pos + 4, end_pos - id_pos - 4);
                }
            }
        }
    }

    std::vector<Subtitle> subs;
    for (auto& obj : objects) {
        if (obj.text.empty() || obj.text.rfind(L"<?", 0) == 0) continue;

        bool has_psdtoolkit = false;
        for (const auto& eff : obj.effects) {
            if (eff.find("PSDToolKit") != std::string::npos) {
                has_psdtoolkit = true; break;
            }
        }
        if (!has_psdtoolkit) continue;

        std::wstring text = obj.text;
        text = replace_all(text, L"\\n", L" ");
        text = replace_all(text, L"\r\n", L" ");
        text = replace_all(text, L"\n", L" ");
        text = replace_all(text, L"\r", L" ");
        text = std::regex_replace(text, std::wregex(L" +"), L" ");
        
        while(!text.empty() && text.front() == L' ') text.erase(0, 1);
        while(!text.empty() && text.back() == L' ') text.pop_back();

        std::wstring char_name;
        if (!obj.char_id.empty()) {
            char_name = obj.char_id;
        } else if (obj.layer != -1) {
            auto it = layer_to_char_id.find(obj.layer);
            if (it != layer_to_char_id.end()) {
                char_name = it->second;
            }
        }

        int start_f = 0, end_f = 0;
        size_t comma = obj.frame_str.find(L',');
        if (comma != std::wstring::npos) {
            start_f = std::stoi(obj.frame_str.substr(0, comma));
            end_f = std::stoi(obj.frame_str.substr(comma + 1));
        }

        subs.push_back({start_f, end_f, text, char_name});
    }

    std::sort(subs.begin(), subs.end(), [](const Subtitle& a, const Subtitle& b) {
        if (a.start_f != b.start_f) return a.start_f < b.start_f;
        return a.end_f < b.end_f;
    });

    std::set<std::wstring> seen_chars;
    std::wstring last_char;

    for (auto& sub : subs) {
        bool add_prefix = false;
        if (!sub.char_name.empty() && config.add_name_mode != 3) {
            if (config.add_name_mode == 0) {
                add_prefix = true;
            } else if (config.add_name_mode == 1) {
                if (seen_chars.find(sub.char_name) == seen_chars.end()) {
                    add_prefix = true;
                    seen_chars.insert(sub.char_name);
                }
            } else if (config.add_name_mode == 2) {
                if (sub.char_name != last_char) {
                    add_prefix = true;
                }
            }
        }

        if (add_prefix) {
            std::wstring fmt = config.name_format;
            fmt = replace_all(fmt, L"{name}", sub.char_name);
            fmt = replace_all(fmt, L"{text}", sub.text);
            fmt = replace_all(fmt, L"\\n", L"\n");
            sub.text = fmt;
        }

        if (!sub.char_name.empty()) {
            last_char = sub.char_name;
        }
    }

    for (size_t i = 1; i < subs.size(); i++) {
        if (subs[i].start_f <= subs[i-1].start_f) {
            subs[i].start_f = subs[i-1].start_f + 1;
            if (subs[i].end_f <= subs[i].start_f) {
                subs[i].end_f = subs[i].start_f + 1;
            }
        }
    }

    std::sort(subs.begin(), subs.end(), [](const Subtitle& a, const Subtitle& b) {
        if (a.start_f != b.start_f) return a.start_f < b.start_f;
        return a.end_f < b.end_f;
    });

    std::map<std::pair<int, int>, std::vector<std::wstring>> grouped;
    for (const auto& sub : subs) {
        grouped[{sub.start_f, sub.end_f}].push_back(sub.text);
    }

    std::vector<Subtitle> final_subs;
    for (const auto& kv : grouped) {
        std::wstring combined;
        for (size_t i = 0; i < kv.second.size(); i++) {
            combined += kv.second[i];
            if (i < kv.second.size() - 1) combined += L"\n";
        }
        final_subs.push_back({kv.first.first, kv.first.second, combined, L""});
    }

    std::sort(final_subs.begin(), final_subs.end(), [](const Subtitle& a, const Subtitle& b) {
        return a.start_f < b.start_f;
    });

    std::ofstream out(output_srt, std::ios::binary);
    if (!out.is_open()) return false;

    int idx = 1;
    for (const auto& sub : final_subs) {
        std::string start_str = format_time(sub.start_f, fps);
        std::string end_str = format_time(sub.end_f, fps);
        out << idx++ << "\r\n";
        out << start_str << " --> " << end_str << "\r\n";
        out << wstring_to_utf8(sub.text) << "\r\n\r\n";
    }

    return true;
}
