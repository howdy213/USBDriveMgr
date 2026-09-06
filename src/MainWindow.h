#pragma once
#include "WinPch.h"

#include <Windows.h>
#include <shellapi.h>
#include <string>
#include <vector>

class MainWindow {
public:
	MainWindow(HINSTANCE hInstance);
	~MainWindow();

	bool Create();
	void RunMessageLoop();

private:
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
	LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	// 控件创建与布局
	void CreateMenuBar();
	void CreateControls();
	void LayoutControls();

	// 驱动器列表相关
	void RefreshDriveList();
	wchar_t GetSelectedDrive() const;

	// 进程操作
	void AnalyzeDrive();
	void KillSelectedProcesses();
	void SelectAllProcesses();

	// 弹出操作
	void EjectSelectedDrive();
	void ShowTrayMenu();
	void ShowNotification(const std::wstring& title, const std::wstring& message, DWORD flags = NIIF_INFO);
	void AddTrayIcon();
	void RemoveTrayIcon();
	void OnTaskbarCreated();

	// 成员
	HINSTANCE m_hInstance;
	HWND m_hWnd = nullptr;
	HWND m_hComboDrive = nullptr;
	HWND m_hBtnAnalyze = nullptr;
	HWND m_hBtnSelectAll = nullptr;
	HWND m_hBtnKill = nullptr;
	HWND m_hBtnEject = nullptr;
	HWND m_hListProc = nullptr;
	HFONT m_hFont = nullptr;
	HICON m_hIconLarge = nullptr;
	HICON m_hIconSmall = nullptr;
	HICON m_hTrayIcon = nullptr;
	HMENU m_hMenu = nullptr;
	NOTIFYICONDATAW m_nid{};
	UINT m_uTaskbarRestartMsg = 0;
	bool m_bIsAdmin = false;

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