#include "MainWindow.h"
#include <Windows.h>

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	HANDLE hMutex = CreateMutexW(NULL, TRUE, L"USBDriveManager_SingleInstance_Mutex");
	if (hMutex == NULL) {
		// 创建互斥体失败，可能是权限问题或其他错误
	}
	else if (GetLastError() == ERROR_ALREADY_EXISTS) {
		CloseHandle(hMutex);
		return 0;
	}

	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	MainWindow app(hInstance);
	if (app.Create()) {
		app.RunMessageLoop();
	}

	if (hMutex) {
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
	}
	return 0;
}