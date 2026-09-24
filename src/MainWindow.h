#pragma once
#include "WinPch.h"

#include <string>
#include <vector>
#include <filesystem>
#include <shellapi.h>

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance, HANDLE* phSingleInstanceMutex = nullptr);
    ~MainWindow();

    bool Create();
    void RunMessageLoop();

private:
    // ===== 枚举 =====
    enum class TrayIconStyle { Default = 0, Dark = 1, Light = 2 };
    enum class NotifyMode { Disabled = 0, InsertOnly = 1, EjectOnly = 2, All = 3 };
    enum class NotifyEvent { Insert, Eject };
    enum class SystrayMenuState { Shown, Hidden };

    // ===== 窗口过程 =====
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    bool    HandleCommand(int wmId);
    LRESULT HandleTrayIconMessage(LPARAM lParam);

    // ===== 界面 =====
    void CreateMenuBar();
    void CreateControls();
    void LayoutControls();

    // ===== 驱动器 =====
    void     RefreshDriveList();
    wchar_t  GetSelectedDrive() const;
    void     AnalyzeDrive();
    void     SelectAllProcesses();
    void     KillSelectedProcesses();
    void     EjectSelectedDrive();
    void     EjectDriveWithNotification(wchar_t drive);
    void     DetectDriveChanges(const std::vector<wchar_t>& drives);

    // ===== 托盘图标 =====
    void AddTrayIcon();
    void RemoveTrayIcon();
    void ApplyTrayIcon();
    HICON LoadTrayIconHandle() const;
    void ShowEjectMenu();
    void ShowMainMenu();
    void OnTaskbarCreated();

    // ===== 通知 =====
    void ShowNotification(NotifyEvent ev,
        const std::wstring& title,
        const std::wstring& message,
        DWORD flags = NIIF_INFO,
        const std::wstring& clickTarget = L"");

    // ===== 系统托盘菜单（注册表）=====
    void  SetSystrayState(SystrayMenuState state);
    void  ApplySystrayState();
    void  CheckAndEnforceSystrayState();
    void  TemporarilyShowSystray();
    bool  WriteSystrayRegValue(DWORD value);
    DWORD ReadSystrayRegValue() const;

    // ===== 设置 / 配置 =====
    void UpdateSettingsMenu();
    void SetTrayIconStyle(TrayIconStyle style);
    void SetNotifyMode(NotifyMode mode);
    void LoadConfig();
    void SaveConfig();
    void OpenConfigFile();
    void RestartApplication();

    // ===== 成员变量 =====
    HINSTANCE m_hInstance;
    HWND      m_hWnd = nullptr;
    HICON     m_hIconLarge = nullptr;
    HICON     m_hIconSmall = nullptr;
    HICON     m_hTrayIcon = nullptr;
    HFONT     m_hFont = nullptr;
    HMENU     m_hMenu = nullptr;
    HMENU     m_hMenuTrayIcon = nullptr;
    HMENU     m_hMenuNotify = nullptr;
    HMENU     m_hMenuSystray = nullptr;
    bool      m_bIsAdmin = false;
    UINT      m_uTaskbarRestartMsg = 0;
    NOTIFYICONDATAW m_nid = {};
    bool      m_bIgnoreNextLButtonUp = false;
    HANDLE* m_phSingleInstanceMutex = nullptr;

    TrayIconStyle m_trayStyle = TrayIconStyle::Default;
    NotifyMode    m_notifyMode = NotifyMode::EjectOnly;
    std::wstring  m_notifyClickTarget;

    std::vector<wchar_t> m_lastDrives;
    bool                 m_drivesInitialized = false;

    HWND m_hComboDrive = nullptr;
    HWND m_hBtnAnalyze = nullptr;
    HWND m_hBtnSelectAll = nullptr;
    HWND m_hBtnKill = nullptr;
    HWND m_hBtnEject = nullptr;
    HWND m_hListProc = nullptr;

    SystrayMenuState m_systrayState = SystrayMenuState::Shown;
    DWORD            m_systrayShowValue = 31;
    DWORD            m_systrayHideValue = 29;
    bool             m_bPauseSystrayOnce = false;

    // ===== 消息 ID =====
    static constexpr UINT WM_TRAYICON = WM_APP + 1;
    static constexpr UINT IDT_REFRESH_DRIVES = 2001;

    // ===== 控件 ID =====
    static constexpr long long IDC_COMBO_DRIVE = 1001;
    static constexpr long long IDC_BTN_ANALYZE = 1002;
    static constexpr long long IDC_BTN_EJECT = 1003;
    static constexpr long long IDC_LIST_PROC = 1004;
    static constexpr long long IDC_BTN_KILL = 1005;
    static constexpr long long IDC_BTN_SELECTALL = 1006;
    static constexpr long long ID_FILE_EXIT = 4001;
    static constexpr long long ID_HELP_ABOUT = 4002;
    static constexpr long long ID_HELP_GITHUB = 4003;

    static constexpr long long ID_SETTINGS_TRAY_DEFAULT = 4010;
    static constexpr long long ID_SETTINGS_TRAY_DARK = 4011;
    static constexpr long long ID_SETTINGS_TRAY_LIGHT = 4012;
    static constexpr long long ID_SETTINGS_NOTIFY_DISABLED = 4020;
    static constexpr long long ID_SETTINGS_NOTIFY_INSERT = 4021;
    static constexpr long long ID_SETTINGS_NOTIFY_EJECT = 4022;
    static constexpr long long ID_SETTINGS_NOTIFY_ALL = 4023;

    static constexpr long long ID_SETTINGS_OPEN_CONFIG = 4030;
    static constexpr long long ID_SETTINGS_RESTART = 4031;
};