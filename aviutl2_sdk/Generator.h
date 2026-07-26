#pragma once
#include <string>
#include <vector>
#include <map>

struct Config {
    int add_name_mode; // 0: all, 1: first, 2: change, 3: none
    std::wstring name_format;
};

bool GenerateSrt(const std::wstring& aup2_path, const std::wstring& output_srt, const Config& config);
