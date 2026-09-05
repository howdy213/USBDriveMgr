#include "WinPch.h"

#include <shlobj.h> 
#include "MainWindow.h"
#include "USBDriveManager.h"
#include "Resource.h"
#include <shellapi.h>
#include <algorithm>
#include <format>
#include <ranges>

#pragma comment(lib, "Shell32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

MainWindow::MainWindow(HINSTANCE hInstance) : m_hInstance(hInstance) {
	// 加载应用图标（大、小）
	m_hIconLarge = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_USBDRIVEMGR),
		IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
	if (!m_hIconLarge) m_hIconLarge = LoadIcon(nullptr, IDI_APPLICATION);   // 回退到默认图标

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
	wc.hIcon = m_hIconLarge;      // 设置窗口大图标
	wc.hIconSm = m_hIconSmall;    // 设置窗口小图标
	RegisterClassExW(&wc);

	// 创建字体
	m_hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"微软雅黑");

	m_bIsAdmin = IsUserAnAdmin() != FALSE;
}

MainWindow::~MainWindow() {
	if (m_hFont) DeleteObject(m_hFont);
	if (m_hIconLarge) DestroyIcon(m_hIconLarge);
	if (m_hIconSmall) DestroyIcon(m_hIconSmall);
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
		break;

	case WM_SIZE:
		LayoutControls();
		return 0;

	case WM_TRAYICON:
		if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
			ShowTrayMenu();
			return 0;
		}
		else if (lParam == WM_LBUTTONDBLCLK) {
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

void MainWindow::CreateControls() {
	// 标签
	HWND hLabel = CreateWindowExW(0, L"STATIC", L"选择USB驱动器:",
		WS_CHILD | WS_VISIBLE,
		10, 12, 100, 20, m_hWnd, nullptr, m_hInstance, nullptr);
	SendMessage(hLabel, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// 下拉框
	m_hComboDrive = CreateWindowExW(0, L"COMBOBOX", L"",
		WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
		130, 10, 120, 200, m_hWnd, (HMENU)IDC_COMBO_DRIVE, m_hInstance, nullptr);
	SendMessage(m_hComboDrive, WM_SETFONT, (WPARAM)m_hFont, TRUE);

	// 按钮
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

	// 列表框
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

	// 构建显示字符串列表
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

	// 检查当前组合框内容是否变化
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

	// 记住当前选中的盘符
	int sel = SendMessage(m_hComboDrive, CB_GETCURSEL, 0, 0);
	wchar_t selDrive = L'\0';
	if (sel != CB_ERR) {
		if (sel >= 0 && sel < (int)drives.size()) {
			selDrive = drives[sel];
		}
	}

	// 清空并重新填充
	SendMessage(m_hComboDrive, CB_RESETCONTENT, 0, 0);
	for (const auto& item : displayItems) {
		SendMessage(m_hComboDrive, CB_ADDSTRING, 0, (LPARAM)item.c_str());
	}

	// 恢复选中项
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
		ShowNotification(L"USB设备已安全弹出", std::format(L"驱动器 {}: 已成功弹出。", drive));
		RefreshDriveList();
	}
	else if (result == USBDriveManager::EjectError::UserCancel) {
		// 用户取消，什么都不做
	}
	else {
		ShowNotification(L"USB弹出失败", std::format(L"驱动器 {}: 弹出失败，可能仍有占用或设备不支持。", drive), NIIF_ERROR);
	}
}

void MainWindow::AddTrayIcon() {
	m_nid.cbSize = sizeof(NOTIFYICONDATAW);
	m_nid.hWnd = m_hWnd;
	m_nid.uID = 3001;
	m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
	m_nid.uCallbackMessage = WM_TRAYICON;
	m_nid.hIcon = m_hIconSmall;

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

void MainWindow::ShowNotification(const std::wstring& title, const std::wstring& message, DWORD flags) {
	m_nid.cbSize = sizeof(NOTIFYICONDATAW);
	m_nid.uFlags = NIF_INFO;
	m_nid.dwInfoFlags = flags;
	wcsncpy_s(m_nid.szInfoTitle, title.c_str(), _TRUNCATE);
	wcsncpy_s(m_nid.szInfo, message.c_str(), _TRUNCATE);
	Shell_NotifyIconW(NIM_MODIFY, &m_nid);
	m_nid.uFlags = 0;
}

void MainWindow::ShowTrayMenu() {
	HMENU hMenu = CreatePopupMenu();
	if (!hMenu) return;

	AppendMenuW(hMenu, MF_STRING, 1, L"显示主界面");
	AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

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
	if (!drives.empty())
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
	default:
		if ((int)cmd >= cmdBase && (int)cmd < cmdBase + (int)drives.size()) {
			wchar_t drive = drives[cmd - cmdBase];
			auto result = USBDriveManager::SafeEject(drive, m_hWnd);
			if (result == USBDriveManager::EjectError::None) {
				ShowNotification(L"USB设备已安全弹出", std::format(L"驱动器 {}: 已成功弹出。", drive));
				RefreshDriveList();
			}
			else if (result != USBDriveManager::EjectError::UserCancel) {
				ShowNotification(L"USB弹出失败", std::format(L"驱动器 {}: 弹出失败。", drive), NIIF_ERROR);
			}
		}
		break;
	}
}

void MainWindow::OnTaskbarCreated() {
	AddTrayIcon();
}