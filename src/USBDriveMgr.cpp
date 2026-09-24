#include "MainWindow.h"
#include <Windows.h>
#include "WinUtils/WinUtils.h"
using namespace WinUtils;

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = EnsureSingleInstance(true);

    MainWindow app(hInstance, &mutex);
    if (app.Create()) {
        app.RunMessageLoop();
    }

    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return 0;
}