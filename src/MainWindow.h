#pragma once
#include "WinPch.h"

#include <string>
#include <vector>
#include <filesystem>
#include <shellapi.h>

class MainWindow {
public:
	MainWindow(HINSTANCE hInstance);
	~MainWindow();
	bool Create();
	void RunMessageLoop();

private:
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	// 托盘图标样式
	enum class TrayIconStyle { Default = 0, Dark = 1, Light = 2 };
	// 通知显示模式
	enum class NotifyMode { Disabled = 0, InsertOnly = 1, EjectOnly = 2, All = 3 };
	// 通知事件类型
	enum class NotifyEvent { Insert, Eject };

	void CreateMenuBar();
	void CreateControls();
	void LayoutControls();
	void RefreshDriveList();
	wchar_t GetSelectedDrive() const;
	void AnalyzeDrive();
	void KillSelectedProcesses();
	void SelectAllProcesses();
	void EjectSelectedDrive();
	void AddTrayIcon();
	void RemoveTrayIcon();
	void ApplyTrayIcon();
	HICON LoadTrayIconHandle() const;
	void UpdateSettingsMenu();
	void SetTrayIconStyle(TrayIconStyle style);
	void SetNotifyMode(NotifyMode mode);
	void LoadConfig();
	void SaveConfig();
	void DetectDriveChanges(const std::vector<wchar_t>& drives);
	void ShowNotification(NotifyEvent ev, const std::wstring& title, const std::wstring& message, DWORD flags = NIIF_INFO, const std::wstring& clickTarget = L"");
	void ShowEjectMenu();   // 新增：左键单击弹出菜单
	void ShowMainMenu();    // 新增：右键单击主菜单
	void OnTaskbarCreated();

	HINSTANCE m_hInstance;
	HWND m_hWnd = nullptr;
	HICON m_hIconLarge = nullptr;
	HICON m_hIconSmall = nullptr;
	HICON m_hTrayIcon = nullptr;
	HFONT m_hFont = nullptr;
	HMENU m_hMenu = nullptr;
	HMENU m_hMenuTrayIcon = nullptr;
	HMENU m_hMenuNotify = nullptr;
	bool m_bIsAdmin = false;
	UINT m_uTaskbarRestartMsg = 0;
	NOTIFYICONDATAW m_nid = {};
	bool m_bIgnoreNextLButtonUp = false; // 新增：忽略双击后的单击消息

	TrayIconStyle m_trayStyle = TrayIconStyle::Default;
	NotifyMode m_notifyMode = NotifyMode::EjectOnly;
	std::wstring m_notifyClickTarget;   // 单击气泡通知时打开的目标（空则不响应）
	std::vector<wchar_t> m_lastDrives;
	bool m_drivesInitialized = false;

	HWND m_hComboDrive = nullptr;
	HWND m_hBtnAnalyze = nullptr;
	HWND m_hBtnSelectAll = nullptr;
	HWND m_hBtnKill = nullptr;
	HWND m_hBtnEject = nullptr;
	HWND m_hListProc = nullptr;

	static constexpr UINT WM_TRAYICON = WM_APP + 1;
	static constexpr UINT IDT_REFRESH_DRIVES = 2001;

	// 控件 ID
	static constexpr long long IDC_COMBO_DRIVE = 1001;
	static constexpr long long IDC_BTN_ANALYZE = 1002;
	static constexpr long long IDC_BTN_EJECT = 1003;
	static constexpr long long IDC_LIST_PROC = 1004;
	static constexpr long long IDC_BTN_KILL = 1005;
	static constexpr long long IDC_BTN_SELECTALL = 1006;
	static constexpr long long ID_FILE_EXIT = 4001;
	static constexpr long long ID_HELP_ABOUT = 4002;
	static constexpr long long ID_HELP_GITHUB = 4003;

	// 设置菜单命令 ID（托盘图标三选一、通知模式四选一，各自连续，便于单选勾选）
	static constexpr long long ID_SETTINGS_TRAY_DEFAULT = 4010;
	static constexpr long long ID_SETTINGS_TRAY_DARK = 4011;
	static constexpr long long ID_SETTINGS_TRAY_LIGHT = 4012;
	static constexpr long long ID_SETTINGS_NOTIFY_DISABLED = 4020;
	static constexpr long long ID_SETTINGS_NOTIFY_INSERT = 4021;
	static constexpr long long ID_SETTINGS_NOTIFY_EJECT = 4022;
	static constexpr long long ID_SETTINGS_NOTIFY_ALL = 4023;
};