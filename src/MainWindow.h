#pragma once
#include "WinPch.h"

#include <string>
#include <vector>
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
	void ShowNotification(const std::wstring& title, const std::wstring& message, DWORD flags = NIIF_INFO);
	void ShowEjectMenu();   // 新增：左键单击弹出菜单
	void ShowMainMenu();    // 新增：右键单击主菜单
	void OnTaskbarCreated();

	HINSTANCE m_hInstance;
	HWND m_hWnd = nullptr;
	HICON m_hIconLarge = nullptr;
	HICON m_hIconSmall = nullptr;
	HFONT m_hFont = nullptr;
	HMENU m_hMenu = nullptr;
	bool m_bIsAdmin = false;
	UINT m_uTaskbarRestartMsg = 0;
	NOTIFYICONDATAW m_nid = {};
	bool m_bIgnoreNextLButtonUp = false; // 新增：忽略双击后的单击消息

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
};