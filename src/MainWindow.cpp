#include "WinPch.h"

#include "MainWindow.h"
#include "USBDriveManager.h"
#include "Resource.h"
#include <shellapi.h>
#include <algorithm>
#include <filesystem>
#include <format>
#include <ranges>

#include "WinUtils/WinUtils.h"
#include "WinUtils/INI.h"

#pragma comment(lib, "Shell32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#define IDT_TRAY_CLICK 1001

MainWindow::MainWindow(HINSTANCE hInstance) : m_hInstance(hInstance) {
	// 加载应用图标（大、小）
	m_hIconLarge = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_USBDRIVEMGR),
		IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
	if (!m_hIconLarge) m_hIconLarge = LoadIcon(nullptr, IDI_APPLICATION);

	m_hIconSmall = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_USBDRIVEMGR),
		IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	if (!m_hIconSmall) m_hIconSmall = LoadIcon(nullptr, IDI_APPLICATION);

	// 注册窗口类
	WNDCLASSEXW wc = { sizeof(wc) };
	wc.lpfnWndProc = MainWindow::WndProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
	wc.lpszClassName = L"USBManagerWindowClass";
	wc.hIcon = m_hIconLarge;
	wc.hIconSm = m_hIconSmall;
	RegisterClassExW(&wc);

	// 创建字体
	m_hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");

	m_bIsAdmin = WinUtils::IsCurrentProcessAdmin();
	m_bIgnoreNextLButtonUp = false;

	LoadConfig();
	ApplyTrayIcon();
}

MainWindow::~MainWindow() {
	if (m_hFont) DeleteObject(m_hFont);
	if (m_hTrayIcon) DestroyIcon(m_hTrayIcon);
	if (m_hIconLarge) DestroyIcon(m_hIconLarge);
	if (m_hIconSmall) DestroyIcon(m_hIconSmall);
	if (m_hMenu) DestroyMenu(m_hMenu);
}

bool MainWindow::Create() {
	std::wstring title = L"USB设备管理器";
	if (m_bIsAdmin) {
		title += L" (管理员)";
	}

	m_hWnd = CreateWindowExW(0, L"USBManagerWindowClass", title.c_str(),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 700, 500,
		nullptr, nullptr, m_hInstance, this);
	if (!m_hWnd) return false;

	AddTrayIcon();
	m_uTaskbarRestartMsg = RegisterWindowMessageW(L"TaskbarCreated");
	return true;
}

void MainWindow::RunMessageLoop() {
	MSG msg;
	while (GetMessage(&msg, nullptr, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	RemoveTrayIcon();
}

LRESULT CALLBACK MainWindow::WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	MainWindow* self = nullptr;
	if (msg == WM_NCCREATE) {
		auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
		self = static_cast<MainWindow*>(cs->lpCreateParams);
		self->m_hWnd = hWnd;
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}
	else {
		self = reinterpret_cast<MainWindow*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}
	if (self)
		return self->HandleMessage(msg, wParam, lParam);
	return DefWindowProc(hWnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg) {
	case WM_CREATE:
		CreateControls();
		CreateMenuBar();
		RefreshDriveList();
		SetTimer(m_hWnd, IDT_REFRESH_DRIVES, 2000, nullptr);
		return 0;

	case WM_GETMINMAXINFO:
	{
		auto* pMinMax = reinterpret_cast<MINMAXINFO*>(lParam);
		pMinMax->ptMinTrackSize.x = 620;
		pMinMax->ptMinTrackSize.y = 400;
		return 0;
	}

	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		switch (wmId) {
		case IDC_BTN_ANALYZE:
			AnalyzeDrive();
			break;
		case IDC_BTN_SELECTALL:
			SelectAllProcesses();
			break;
		case IDC_BTN_KILL:
			KillSelectedProcesses();
			AnalyzeDrive();
			break;
		case IDC_BTN_EJECT:
			EjectSelectedDrive();
			break;
		case ID_FILE_EXIT:
			DestroyWindow(m_hWnd);
			break;
		case ID_HELP_ABOUT:
			MessageBoxW(m_hWnd,
				L"　　　　USB设备管理器"
				L"　　　　　　　　　　　　　　　　　　\n"
				L"　　　　版本 v1.1.0\n"
				L"　　　　作者：howdy213\n"
				L"　　　　软件在 MIT License 下发布\n",
				L"关于", MB_OK);
			break;
		case ID_HELP_GITHUB:
			WinUtils::RunExternalProgram(L"https://github.com/howdy213/USBDriveMgr", L"open");
			break;
		case ID_SETTINGS_TRAY_DEFAULT: SetTrayIconStyle(TrayIconStyle::Default); break;
		case ID_SETTINGS_TRAY_DARK:    SetTrayIconStyle(TrayIconStyle::Dark); break;
		case ID_SETTINGS_TRAY_LIGHT:   SetTrayIconStyle(TrayIconStyle::Light); break;
		case ID_SETTINGS_NOTIFY_DISABLED: SetNotifyMode(NotifyMode::Disabled); break;
		case ID_SETTINGS_NOTIFY_INSERT:   SetNotifyMode(NotifyMode::InsertOnly); break;
		case ID_SETTINGS_NOTIFY_EJECT:    SetNotifyMode(NotifyMode::EjectOnly); break;
		case ID_SETTINGS_NOTIFY_ALL:      SetNotifyMode(NotifyMode::All); break;
		default:
			return DefWindowProc(m_hWnd, msg, wParam, lParam);
		}
		return 0;
	}
	case WM_CTLCOLORSTATIC:
	case WM_CTLCOLORBTN:
	case WM_CTLCOLORLISTBOX:
	{
		HDC hdcStatic = (HDC)wParam;
		SetTextColor(hdcStatic, RGB(0, 0, 0));
		SetBkMode(hdcStatic, TRANSPARENT);
		return (LRESULT)GetStockObject(WHITE_BRUSH);
	}
	case WM_TIMER:
		if (wParam == IDT_REFRESH_DRIVES) {
			RefreshDriveList();
			return 0;
		}
		else if (wParam == IDT_TRAY_CLICK) {
			KillTimer(m_hWnd, IDT_TRAY_CLICK);
			ShowEjectMenu();
			return 0;
		}
		break;

	case WM_SIZE:
		LayoutControls();
		return 0;

	case WM_TRAYICON:
		if (lParam == NIN_BALLOONUSERCLICK) {
			// 单击气泡通知：打开对应目标（如插入的驱动器）
			if (!m_notifyClickTarget.empty()) {
				WinUtils::RunExternalProgram(m_notifyClickTarget, L"open");
				m_notifyClickTarget.clear();
			}
			return 0;
		}
		else if (lParam == WM_LBUTTONUP) {
			if (m_bIgnoreNextLButtonUp) {
				m_bIgnoreNextLButtonUp = false;
				return 0;
			}
			// 延迟弹出左键菜单，以便区分双击
			SetTimer(m_hWnd, IDT_TRAY_CLICK, GetDoubleClickTime(), nullptr);
			return 0;
		}
		else if (lParam == WM_RBUTTONUP) {
			ShowMainMenu();
			return 0;
		}
		else if (lParam == WM_LBUTTONDBLCLK) {
			m_bIgnoreNextLButtonUp = true;
			KillTimer(m_hWnd, IDT_TRAY_CLICK);
			ShowWindow(m_hWnd, SW_SHOW);
			SetForegroundWindow(m_hWnd);
			return 0;
		}
		break;

	case WM_CLOSE:
		ShowWindow(m_hWnd, SW_HIDE);
		return 0;

	case WM_DESTROY:
		KillTimer(m_hWnd, IDT_REFRESH_DRIVES);
		KillTimer(m_hWnd, IDT_TRAY_CLICK);
		PostQuitMessage(0);
		return 0;

	default:
		if (msg == m_uTaskbarRestartMsg) {
			AddTrayIcon();
			return 0;
		}
		break;
	}
	return DefWindowProc(m_hWnd, msg, wParam, lParam);
}

void MainWindow::CreateMenuBar() {
	HMENU hMenuBar = CreateMenu();
	if (!hMenuBar) return;

	HMENU hFileMenu = CreatePopupMenu();
	AppendMenuW(hFileMenu, MF_STRING, ID_FILE_EXIT, L"退出程序");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hFileMenu, L"文件");

	// 设置菜单：托盘图标（三选一）、通知显示（四选一）
	HMENU hSettingsMenu = CreatePopupMenu();

	m_hMenuTrayIcon = CreatePopupMenu();
	AppendMenuW(m_hMenuTrayIcon, MF_STRING, ID_SETTINGS_TRAY_DEFAULT, L"默认");
	AppendMenuW(m_hMenuTrayIcon, MF_STRING, ID_SETTINGS_TRAY_DARK, L"深色");
	AppendMenuW(m_hMenuTrayIcon, MF_STRING, ID_SETTINGS_TRAY_LIGHT, L"浅色");
	AppendMenuW(hSettingsMenu, MF_POPUP, (UINT_PTR)m_hMenuTrayIcon, L"托盘图标");

	m_hMenuNotify = CreatePopupMenu();
	AppendMenuW(m_hMenuNotify, MF_STRING, ID_SETTINGS_NOTIFY_DISABLED, L"全部禁用");
	AppendMenuW(m_hMenuNotify, MF_STRING, ID_SETTINGS_NOTIFY_INSERT, L"仅插入时");
	AppendMenuW(m_hMenuNotify, MF_STRING, ID_SETTINGS_NOTIFY_EJECT, L"仅弹出时");
	AppendMenuW(m_hMenuNotify, MF_STRING, ID_SETTINGS_NOTIFY_ALL, L"全部启用");
	AppendMenuW(hSettingsMenu, MF_POPUP, (UINT_PTR)m_hMenuNotify, L"通知显示");

	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hSettingsMenu, L"设置");

	HMENU hHelpMenu = CreatePopupMenu();
	AppendMenuW(hHelpMenu, MF_STRING, ID_HELP_ABOUT, L"关于");
	AppendMenuW(hHelpMenu, MF_STRING, ID_HELP_GITHUB, L"转至 GitHub 仓库");
	AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hHelpMenu, L"帮助");

	SetMenu(m_hWnd, hMenuBar);
	m_hMenu = hMenuBar;
	UpdateSettingsMenu();
}

void MainWindow::CreateControls() {
	HWND hLabel = CreateWindowExW(0, L"STATIC", L"选择USB驱动器:",
		WS_CHILD | WS_VISIBLE,
		10, 12, 100, 20, m_hWnd, nullptr, m_hInstance, nullptr);
	SendMessage(hLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hComboDrive = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		130, 10, 120, 200, m_hWnd, (HMENU)IDC_COMBO_DRIVE, m_hInstance, nullptr);
	SendMessage(m_hComboDrive, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hBtnAnalyze = CreateWindowExW(0, L"BUTTON", L"分析占用",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		260, 10, 80, 25, m_hWnd, (HMENU)IDC_BTN_ANALYZE, m_hInstance, nullptr);
	SendMessage(m_hBtnAnalyze, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hBtnSelectAll = CreateWindowExW(0, L"BUTTON", L"全选",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		350, 10, 60, 25, m_hWnd, (HMENU)IDC_BTN_SELECTALL, m_hInstance, nullptr);
	SendMessage(m_hBtnSelectAll, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hBtnKill = CreateWindowExW(0, L"BUTTON", L"终止选中",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		420, 10, 80, 25, m_hWnd, (HMENU)IDC_BTN_KILL, m_hInstance, nullptr);
	SendMessage(m_hBtnKill, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hBtnEject = CreateWindowExW(0, L"BUTTON", L"弹出并退出",
		WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
		510, 10, 80, 25, m_hWnd, (HMENU)IDC_BTN_EJECT, m_hInstance, nullptr);
	SendMessage(m_hBtnEject, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	m_hListProc = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
		WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL | LBS_EXTENDEDSEL,
		10, 50, 580, 400, m_hWnd, (HMENU)IDC_LIST_PROC, m_hInstance, nullptr);
	SendMessage(m_hListProc, WM_SETFONT, (WPARAM)m_hFont, TRUE);
}

void MainWindow::LayoutControls() {
	RECT rc;
	GetClientRect(m_hWnd, &rc);
	int width = rc.right - rc.left;
	int height = rc.bottom - rc.top;

	int margin = 10;
	int yTop = 10;
	int comboWidth = 120;
	int btnWidth = 80;
	int btnHeight = 25;

	MoveWindow(m_hComboDrive, 130, yTop, comboWidth, 200, TRUE);
	MoveWindow(m_hBtnAnalyze, 260, yTop, btnWidth, btnHeight, TRUE);
	MoveWindow(m_hBtnSelectAll, 350, yTop, 60, btnHeight, TRUE);
	MoveWindow(m_hBtnKill, 420, yTop, btnWidth, btnHeight, TRUE);
	MoveWindow(m_hBtnEject, 510, yTop, btnWidth, btnHeight, TRUE);

	int listY = yTop + btnHeight + 10;
	MoveWindow(m_hListProc, margin, listY, width - 2 * margin, height - listY - margin, TRUE);
}

void MainWindow::RefreshDriveList() {
	std::vector<wchar_t> drives = USBDriveManager::GetUSBDrives();
	DetectDriveChanges(drives);

	std::vector<std::wstring> displayItems;
	for (wchar_t d : drives) {
		std::wstring label = USBDriveManager::GetVolumeLabel(d);
		if (label.empty()) {
			displayItems.push_back(std::format(L"{}:", d));
		}
		else {
			displayItems.push_back(std::format(L"{}: ({})", d, label));
		}
	}

	int currentCount = SendMessage(m_hComboDrive, CB_GETCOUNT, 0, 0);
	bool changed = (currentCount != (int)displayItems.size());
	if (!changed) {
		for (int i = 0; i < currentCount; ++i) {
			wchar_t buf[256] = { 0 };
			SendMessage(m_hComboDrive, CB_GETLBTEXT, i, (LPARAM)buf);
			if (displayItems[i] != buf) {
				changed = true;
				break;
			}
		}
	}
	if (!changed) return;

	int sel = SendMessage(m_hComboDrive, CB_GETCURSEL, 0, 0);
	wchar_t selDrive = L'\0';
	if (sel != CB_ERR) {
		if (sel >= 0 && sel < (int)drives.size()) {
			selDrive = drives[sel];
		}
	}

	SendMessage(m_hComboDrive, CB_RESETCONTENT, 0, 0);
	for (const auto& item : displayItems) {
		SendMessage(m_hComboDrive, CB_ADDSTRING, 0, (LPARAM)item.c_str());
	}

	if (!drives.empty()) {
		int idx = CB_ERR;
		if (selDrive) {
			auto it = std::ranges::find(drives, selDrive);
			if (it != drives.end()) idx = std::distance(drives.begin(), it);
		}
		if (idx == CB_ERR) idx = 0;
		SendMessage(m_hComboDrive, CB_SETCURSEL, idx, 0);
	}
}

wchar_t MainWindow::GetSelectedDrive() const {
	wchar_t buf[16] = { 0 };
	int idx = SendMessage(m_hComboDrive, CB_GETCURSEL, 0, 0);
	if (idx == CB_ERR) return L'\0';
	SendMessage(m_hComboDrive, CB_GETLBTEXT, idx, (LPARAM)buf);
	if (buf[0] && buf[1] == L':') return buf[0];
	return L'\0';
}

void MainWindow::AnalyzeDrive() {
	wchar_t drive = GetSelectedDrive();
	if (drive == L'\0') return;

	SendMessage(m_hListProc, LB_RESETCONTENT, 0, 0);
	std::vector<DWORD> pids = USBDriveManager::GetProcessesUsingDrive(drive);
	if (pids.empty()) {
		SendMessage(m_hListProc, LB_ADDSTRING, 0, (LPARAM)L"无占用进程");
	}
	else {
		for (DWORD pid : pids) {
			std::wstring path = USBDriveManager::GetProcessImagePath(pid);
			std::wstring line = std::format(L"PID {} - {}", pid, path.empty() ? L"<无法获取路径>" : path);
			int index = SendMessage(m_hListProc, LB_ADDSTRING, 0, (LPARAM)line.c_str());
			SendMessage(m_hListProc, LB_SETITEMDATA, index, (LPARAM)pid);
		}
	}
}

void MainWindow::KillSelectedProcesses() {
	int count = SendMessage(m_hListProc, LB_GETSELCOUNT, 0, 0);
	if (count == 0) {
		MessageBoxW(m_hWnd, L"请先在列表中选择要终止的进程。", L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	std::vector<int> selectedIndices(count);
	SendMessage(m_hListProc, LB_GETSELITEMS, count, (LPARAM)selectedIndices.data());

	std::vector<DWORD> pidsToKill;
	for (int idx : selectedIndices) {
		DWORD pid = (DWORD)SendMessage(m_hListProc, LB_GETITEMDATA, idx, 0);
		if (pid != 0) pidsToKill.push_back(pid);
	}

	if (pidsToKill.empty()) {
		MessageBoxW(m_hWnd, L"无法获取进程ID。", L"错误", MB_OK | MB_ICONERROR);
		return;
	}

	if (MessageBoxW(m_hWnd, L"确定要终止选中的进程吗？", L"确认", MB_YESNO | MB_ICONWARNING) != IDYES)
		return;

	if (USBDriveManager::TerminateProcesses(pidsToKill))
		MessageBoxW(m_hWnd, L"选中的进程已终止。", L"成功", MB_OK | MB_ICONINFORMATION);
	else
		MessageBoxW(m_hWnd, L"部分进程终止失败，可能需要管理员权限。", L"警告", MB_OK | MB_ICONWARNING);
}

void MainWindow::SelectAllProcesses() {
	int count = SendMessage(m_hListProc, LB_GETCOUNT, 0, 0);
	SendMessage(m_hListProc, LB_SELITEMRANGEEX, 0, count - 1);
}

void MainWindow::EjectSelectedDrive() {
	wchar_t drive = GetSelectedDrive();
	if (drive == L'\0') {
		MessageBoxW(m_hWnd, L"请先选择一个USB驱动器。", L"提示", MB_OK | MB_ICONINFORMATION);
		return;
	}

	auto result = USBDriveManager::SafeEject(drive, m_hWnd);
	if (result == USBDriveManager::EjectError::None) {
		ShowNotification(NotifyEvent::Eject, L"USB设备已安全弹出", std::format(L"驱动器 {}: 已成功弹出。", drive));
		RefreshDriveList();
	}
	else if (result == USBDriveManager::EjectError::UserCancel) {
		// 用户取消，什么都不做
	}
	else {
		ShowNotification(NotifyEvent::Eject, L"USB弹出失败", std::format(L"驱动器 {}: 弹出失败，可能仍有占用或设备不支持。", drive), NIIF_ERROR);
	}
}

void MainWindow::AddTrayIcon() {
	m_nid.cbSize = sizeof(NOTIFYICONDATAW);
	m_nid.hWnd = m_hWnd;
	m_nid.uID = 3001;
	m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_nid.uCallbackMessage = WM_TRAYICON;
	m_nid.hIcon = m_hTrayIcon;

	std::wstring tip = L"USB设备管理器";
	if (m_bIsAdmin) {
		tip += L" (管理员)";
	}
	wcsncpy_s(m_nid.szTip, tip.c_str(), _TRUNCATE);

	Shell_NotifyIconW(NIM_ADD, &m_nid);
}

void MainWindow::RemoveTrayIcon() {
	Shell_NotifyIconW(NIM_DELETE, &m_nid);
}

void MainWindow::ShowNotification(NotifyEvent ev, const std::wstring& title, const std::wstring& message, DWORD flags, const std::wstring& clickTarget) {
	if (m_notifyMode == NotifyMode::Disabled) return;
	if (m_notifyMode == NotifyMode::InsertOnly && ev != NotifyEvent::Insert) return;
	if (m_notifyMode == NotifyMode::EjectOnly && ev != NotifyEvent::Eject) return;

	m_notifyClickTarget = clickTarget;

	m_nid.cbSize = sizeof(NOTIFYICONDATAW);
	m_nid.uFlags = NIF_INFO;
	m_nid.dwInfoFlags = flags;
	wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
	wcsncpy_s(m_nid.szInfo, message.c_str(), _TRUNCATE);
	Shell_NotifyIconW(NIM_MODIFY, &m_nid);
	m_nid.uFlags = 0;
}

// 依据托盘图标样式重新加载图标并刷新托盘（窗口尚未创建时仅加载句柄）
void MainWindow::ApplyTrayIcon() {
	HICON hNew = LoadTrayIconHandle();
	HICON hOld = m_hTrayIcon;
	m_hTrayIcon = hNew;

	if (m_hWnd) {
		m_nid.hIcon = m_hTrayIcon;
		m_nid.uFlags = NIF_ICON;
		Shell_NotifyIconW(NIM_MODIFY, &m_nid);
		m_nid.uFlags = 0;
	}

	if (hOld && hOld != hNew) DestroyIcon(hOld);
}

HICON MainWindow::LoadTrayIconHandle() const {
	int resId = IDI_USBDRIVEMGR;
	if (m_trayStyle == TrayIconStyle::Dark) resId = IDI_TRAY_DARK;
	else if (m_trayStyle == TrayIconStyle::Light) resId = IDI_TRAY_LIGHT;

	HICON h = (HICON)LoadImageW(m_hInstance, MAKEINTRESOURCEW(resId),
		IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	if (!h) h = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
	return h;
}

// 勾选当前设置项（单选）
void MainWindow::UpdateSettingsMenu() {
	if (!m_hMenu) return;
	CheckMenuRadioItem(m_hMenuTrayIcon, (UINT)ID_SETTINGS_TRAY_DEFAULT, (UINT)ID_SETTINGS_TRAY_LIGHT,
		(UINT)(ID_SETTINGS_TRAY_DEFAULT + (long long)m_trayStyle), MF_BYCOMMAND);
	CheckMenuRadioItem(m_hMenuNotify, (UINT)ID_SETTINGS_NOTIFY_DISABLED, (UINT)ID_SETTINGS_NOTIFY_ALL,
		(UINT)(ID_SETTINGS_NOTIFY_DISABLED + (long long)m_notifyMode), MF_BYCOMMAND);
}

void MainWindow::SetTrayIconStyle(TrayIconStyle style) {
	m_trayStyle = style;
	ApplyTrayIcon();
	UpdateSettingsMenu();
	SaveConfig();
}

void MainWindow::SetNotifyMode(NotifyMode mode) {
	m_notifyMode = mode;
	UpdateSettingsMenu();
	SaveConfig();
}

// 读取设置：<程序目录>\config\config.ini
void MainWindow::LoadConfig() {
	std::filesystem::path path = WinUtils::GetCurrentProcessFSDir() / L"config" / L"config.ini";
	WinUtils::INIFile file(path);
	WinUtils::INIStructure ini;
	if (!file.read(ini)) return;

	std::wstring tray = ini[L"Settings"].get(L"TrayIcon");
	if (tray == L"dark") m_trayStyle = TrayIconStyle::Dark;
	else if (tray == L"light") m_trayStyle = TrayIconStyle::Light;
	else m_trayStyle = TrayIconStyle::Default;

	std::wstring notify = ini[L"Settings"].get(L"Notify");
	if (notify == L"disabled") m_notifyMode = NotifyMode::Disabled;
	else if (notify == L"insert") m_notifyMode = NotifyMode::InsertOnly;
	else if (notify == L"all") m_notifyMode = NotifyMode::All;
	else m_notifyMode = NotifyMode::EjectOnly;
}

void MainWindow::SaveConfig() {
	std::filesystem::path dir = WinUtils::GetCurrentProcessFSDir() / L"config";
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);

	const wchar_t* tray = L"default";
	if (m_trayStyle == TrayIconStyle::Dark) tray = L"dark";
	else if (m_trayStyle == TrayIconStyle::Light) tray = L"light";

	const wchar_t* notify = L"eject";
	if (m_notifyMode == NotifyMode::Disabled) notify = L"disabled";
	else if (m_notifyMode == NotifyMode::InsertOnly) notify = L"insert";
	else if (m_notifyMode == NotifyMode::All) notify = L"all";

	WinUtils::INIStructure ini;
	ini[L"Settings"][L"TrayIcon"] = tray;
	ini[L"Settings"][L"Notify"] = notify;

	WinUtils::INIFile file(dir / L"config.ini");
	file.generate(ini, true);
}

// 对比上次刷新结果，对新出现的驱动器发送插入通知
void MainWindow::DetectDriveChanges(const std::vector<wchar_t>& drives) {
	if (!m_drivesInitialized) {
		m_lastDrives = drives;
		m_drivesInitialized = true;
		return;
	}

	for (wchar_t d : drives) {
		if (std::ranges::find(m_lastDrives, d) != m_lastDrives.end()) continue;
		std::wstring label = USBDriveManager::GetVolumeLabel(d);
		std::wstring msg = label.empty()
			? std::format(L"驱动器 {}: 已插入，单击以打开文件资源管理器。", d)
			: std::format(L"驱动器 {}: ({}) 已插入，单击以打开文件资源管理器。", d, label);
		ShowNotification(NotifyEvent::Insert, L"USB设备已插入", msg, NIIF_INFO, std::format(L"{}:\\", d));
	}
	m_lastDrives = drives;
}

// 左键单击：仅显示弹出驱动器菜单
void MainWindow::ShowEjectMenu() {
	HMENU hMenu = CreatePopupMenu();
	if (!hMenu) return;

	std::vector<wchar_t> drives = USBDriveManager::GetUSBDrives();
	const int cmdBase = 100;
	for (size_t i = 0; i < drives.size(); ++i) {
		std::wstring label = USBDriveManager::GetVolumeLabel(drives[i]);
		std::wstring item;
		if (label.empty())
			item = std::format(L"弹出 {}:", drives[i]);
		else
			item = std::format(L"弹出 {}: ({})", drives[i], label);
		AppendMenuW(hMenu, MF_STRING, cmdBase + i, item.c_str());
	}
	if (drives.empty()) {
		AppendMenuW(hMenu, MF_STRING | MF_GRAYED, 0, L"无USB驱动器");
	}

	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(m_hWnd);
	UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
	PostMessageW(m_hWnd, WM_NULL, 0, 0);
	DestroyMenu(hMenu);

	if ((int)cmd >= cmdBase && (int)cmd < cmdBase + (int)drives.size()) {
		wchar_t drive = drives[cmd - cmdBase];
		auto result = USBDriveManager::SafeEject(drive, m_hWnd);
		if (result == USBDriveManager::EjectError::None) {
			ShowNotification(NotifyEvent::Eject, L"USB设备已安全弹出", std::format(L"驱动器 {}: 已成功弹出。", drive));
			RefreshDriveList();
		}
		else if (result != USBDriveManager::EjectError::UserCancel) {
			ShowNotification(NotifyEvent::Eject, L"USB弹出失败", std::format(L"驱动器 {}: 弹出失败。", drive), NIIF_ERROR);
		}
	}
}

// 右键单击：显示主菜单（显示主界面、退出）
void MainWindow::ShowMainMenu() {
	HMENU hMenu = CreatePopupMenu();
	if (!hMenu) return;

	AppendMenuW(hMenu, MF_STRING, 1, L"显示主菜单");
	AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
	AppendMenuW(hMenu, MF_STRING, 2, L"退出");

	POINT pt;
	GetCursorPos(&pt);
	SetForegroundWindow(m_hWnd);
	UINT cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hWnd, nullptr);
	PostMessageW(m_hWnd, WM_NULL, 0, 0);
	DestroyMenu(hMenu);

	switch (cmd) {
	case 1:
		ShowWindow(m_hWnd, SW_SHOW);
		SetForegroundWindow(m_hWnd);
		break;
	case 2:
		DestroyWindow(m_hWnd);
		break;
	}
}

void MainWindow::OnTaskbarCreated() {
	AddTrayIcon();
}