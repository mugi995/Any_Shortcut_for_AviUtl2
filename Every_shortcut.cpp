#include "Every_shortcut.h"
#include <windows.h>
#include <sstream>

EDIT_HANDLE* g_edit_handle = nullptr;
LOG_HANDLE* g_logger = nullptr;
CONFIG_HANDLE* g_config = nullptr;
HOST_APP_TABLE* g_host = nullptr;
HINSTANCE g_hInstance = nullptr;
HWND g_setting_hwnd = nullptr;
std::vector<ShortcutCommand> g_commands;

COMMON_PLUGIN_TABLE common_plugin_table = {
    L"Any_Shortcut_for_AviUtl2",
    L"Any_Shortcut_for_AviUtl2 version 1.00"
};

EXTERN_C __declspec(dllexport) DWORD RequiredVersion() { return 2003300; }
EXTERN_C __declspec(dllexport) void InitializeLogger(LOG_HANDLE* handle) { g_logger = handle; }
EXTERN_C __declspec(dllexport) void InitializeConfig(CONFIG_HANDLE* handle) { g_config = handle; }
EXTERN_C __declspec(dllexport) bool InitializePlugin(DWORD version) { return true; }

EXTERN_C __declspec(dllexport) void UninitializePlugin() {
}

EXTERN_C __declspec(dllexport) COMMON_PLUGIN_TABLE* GetCommonPluginTable(void) {
    return &common_plugin_table;
}

#define MAX_SLOTS 128

static int g_slot_to_cmd_id[MAX_SLOTS] = {};
static int g_slot_count = 0;

#define DEFINE_SLOT(n) \
    static void Slot_##n(EDIT_SECTION* edit) { \
        int id = g_slot_to_cmd_id[n]; \
        if (g_logger) { \
            std::wstring msg = L"[Slot_" + std::to_wstring(n) + L"] cmd_id=" + std::to_wstring(id); \
            g_logger->log(g_logger, msg.c_str()); \
        } \
        CommandExecutor::ExecuteCommandInSection(id, edit); \
    }

DEFINE_SLOT(0)   DEFINE_SLOT(1)   DEFINE_SLOT(2)   DEFINE_SLOT(3)
DEFINE_SLOT(4)   DEFINE_SLOT(5)   DEFINE_SLOT(6)   DEFINE_SLOT(7)
DEFINE_SLOT(8)   DEFINE_SLOT(9)   DEFINE_SLOT(10)  DEFINE_SLOT(11)
DEFINE_SLOT(12)  DEFINE_SLOT(13)  DEFINE_SLOT(14)  DEFINE_SLOT(15)
DEFINE_SLOT(16)  DEFINE_SLOT(17)  DEFINE_SLOT(18)  DEFINE_SLOT(19)
DEFINE_SLOT(20)  DEFINE_SLOT(21)  DEFINE_SLOT(22)  DEFINE_SLOT(23)
DEFINE_SLOT(24)  DEFINE_SLOT(25)  DEFINE_SLOT(26)  DEFINE_SLOT(27)
DEFINE_SLOT(28)  DEFINE_SLOT(29)  DEFINE_SLOT(30)  DEFINE_SLOT(31)
DEFINE_SLOT(32)  DEFINE_SLOT(33)  DEFINE_SLOT(34)  DEFINE_SLOT(35)
DEFINE_SLOT(36)  DEFINE_SLOT(37)  DEFINE_SLOT(38)  DEFINE_SLOT(39)
DEFINE_SLOT(40)  DEFINE_SLOT(41)  DEFINE_SLOT(42)  DEFINE_SLOT(43)
DEFINE_SLOT(44)  DEFINE_SLOT(45)  DEFINE_SLOT(46)  DEFINE_SLOT(47)
DEFINE_SLOT(48)  DEFINE_SLOT(49)  DEFINE_SLOT(50)  DEFINE_SLOT(51)
DEFINE_SLOT(52)  DEFINE_SLOT(53)  DEFINE_SLOT(54)  DEFINE_SLOT(55)
DEFINE_SLOT(56)  DEFINE_SLOT(57)  DEFINE_SLOT(58)  DEFINE_SLOT(59)
DEFINE_SLOT(60)  DEFINE_SLOT(61)  DEFINE_SLOT(62)  DEFINE_SLOT(63)
DEFINE_SLOT(64)  DEFINE_SLOT(65)  DEFINE_SLOT(66)  DEFINE_SLOT(67)
DEFINE_SLOT(68)  DEFINE_SLOT(69)  DEFINE_SLOT(70)  DEFINE_SLOT(71)
DEFINE_SLOT(72)  DEFINE_SLOT(73)  DEFINE_SLOT(74)  DEFINE_SLOT(75)
DEFINE_SLOT(76)  DEFINE_SLOT(77)  DEFINE_SLOT(78)  DEFINE_SLOT(79)
DEFINE_SLOT(80)  DEFINE_SLOT(81)  DEFINE_SLOT(82)  DEFINE_SLOT(83)
DEFINE_SLOT(84)  DEFINE_SLOT(85)  DEFINE_SLOT(86)  DEFINE_SLOT(87)
DEFINE_SLOT(88)  DEFINE_SLOT(89)  DEFINE_SLOT(90)  DEFINE_SLOT(91)
DEFINE_SLOT(92)  DEFINE_SLOT(93)  DEFINE_SLOT(94)  DEFINE_SLOT(95)
DEFINE_SLOT(96)  DEFINE_SLOT(97)  DEFINE_SLOT(98)  DEFINE_SLOT(99)
DEFINE_SLOT(100) DEFINE_SLOT(101) DEFINE_SLOT(102) DEFINE_SLOT(103)
DEFINE_SLOT(104) DEFINE_SLOT(105) DEFINE_SLOT(106) DEFINE_SLOT(107)
DEFINE_SLOT(108) DEFINE_SLOT(109) DEFINE_SLOT(110) DEFINE_SLOT(111)
DEFINE_SLOT(112) DEFINE_SLOT(113) DEFINE_SLOT(114) DEFINE_SLOT(115)
DEFINE_SLOT(116) DEFINE_SLOT(117) DEFINE_SLOT(118) DEFINE_SLOT(119)
DEFINE_SLOT(120) DEFINE_SLOT(121) DEFINE_SLOT(122) DEFINE_SLOT(123)
DEFINE_SLOT(124) DEFINE_SLOT(125) DEFINE_SLOT(126) DEFINE_SLOT(127)

static void(*g_slots[MAX_SLOTS])(EDIT_SECTION*) = {
    Slot_0,   Slot_1,   Slot_2,   Slot_3,   Slot_4,   Slot_5,   Slot_6,   Slot_7,
    Slot_8,   Slot_9,   Slot_10,  Slot_11,  Slot_12,  Slot_13,  Slot_14,  Slot_15,
    Slot_16,  Slot_17,  Slot_18,  Slot_19,  Slot_20,  Slot_21,  Slot_22,  Slot_23,
    Slot_24,  Slot_25,  Slot_26,  Slot_27,  Slot_28,  Slot_29,  Slot_30,  Slot_31,
    Slot_32,  Slot_33,  Slot_34,  Slot_35,  Slot_36,  Slot_37,  Slot_38,  Slot_39,
    Slot_40,  Slot_41,  Slot_42,  Slot_43,  Slot_44,  Slot_45,  Slot_46,  Slot_47,
    Slot_48,  Slot_49,  Slot_50,  Slot_51,  Slot_52,  Slot_53,  Slot_54,  Slot_55,
    Slot_56,  Slot_57,  Slot_58,  Slot_59,  Slot_60,  Slot_61,  Slot_62,  Slot_63,
    Slot_64,  Slot_65,  Slot_66,  Slot_67,  Slot_68,  Slot_69,  Slot_70,  Slot_71,
    Slot_72,  Slot_73,  Slot_74,  Slot_75,  Slot_76,  Slot_77,  Slot_78,  Slot_79,
    Slot_80,  Slot_81,  Slot_82,  Slot_83,  Slot_84,  Slot_85,  Slot_86,  Slot_87,
    Slot_88,  Slot_89,  Slot_90,  Slot_91,  Slot_92,  Slot_93,  Slot_94,  Slot_95,
    Slot_96,  Slot_97,  Slot_98,  Slot_99,  Slot_100, Slot_101, Slot_102, Slot_103,
    Slot_104, Slot_105, Slot_106, Slot_107, Slot_108, Slot_109, Slot_110, Slot_111,
    Slot_112, Slot_113, Slot_114, Slot_115, Slot_116, Slot_117, Slot_118, Slot_119,
    Slot_120, Slot_121, Slot_122, Slot_123, Slot_124, Slot_125, Slot_126, Slot_127,
};

static std::vector<std::wstring> g_menu_paths;

static void OnConfigMenuCallback(HWND hwnd, HINSTANCE dll_hinst) {
    SettingDialog::ShowSettingWindow(hwnd, dll_hinst);
}

EXTERN_C __declspec(dllexport) void RegisterPlugin(HOST_APP_TABLE* host) {
    if (!host) return;
    g_host = host;
    g_edit_handle = host->create_edit_handle();

    // SplitFilters 方式: set_plugin_information も呼ぶ
    host->set_plugin_information(L"Any_Shortcut_for_AviUtl2 version 1.00");

    ConfigManager::LoadConfig();

    g_menu_paths.clear();
    g_slot_count = 0;
    for (const auto& cmd : g_commands) {
        if (g_slot_count >= MAX_SLOTS) {
            if (g_logger) {
                std::wstring msg = L"Any_Shortcut_for_AviUtl2: MAX_SLOTS (" + std::to_wstring(MAX_SLOTS) + L") exceeded! "
                    + std::to_wstring(g_commands.size() - g_slot_count) + L" commands not registered.";
                g_logger->warn(g_logger, msg.c_str());
            }
            break;
        }

        g_menu_paths.push_back(L"Any_Shortcut_for_AviUtl2\\" + cmd.name);

        g_slot_to_cmd_id[g_slot_count] = cmd.id;
        g_host->register_edit_menu(g_menu_paths.back().c_str(), g_slots[g_slot_count]);
        g_slot_count++;
    }

    g_host->register_config_menu(L"Any_Shortcut_for_AviUtl2設定", OnConfigMenuCallback);

    if (g_logger) {
        std::wstring msg = L"Any_Shortcut_for_AviUtl2 registered " + std::to_wstring(g_slot_count) + L" commands.";
        g_logger->log(g_logger, msg.c_str());
    }
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hInstance = hModule;
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
