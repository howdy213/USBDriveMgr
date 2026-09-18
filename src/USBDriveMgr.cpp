#include "MainWindow.h"
#include <Windows.h>
#include "WinUtils/WinUtils.h"
using namespace WinUtils;
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
	EnsureSingleInstance(true);
	SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	MainWindow app(hInstance);
	if (app.Create()) {
		app.RunMessageLoop();
	}
	return 0;
}