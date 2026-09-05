#pragma once
#include <Windows.h>
#include <vector>
#include <string>
#include <expected>
#include <format>
#include <optional>
#include <cfgmgr32.h>

class USBDriveManager {
public:
	// 错误类型
	enum class EjectError {
		None,
		UserCancel,
		ProcessTerminateFailed,
		EjectFailed
	};

	// 获取所有 USB 驱动器盘符（如 L'E', L'F'）
	static std::vector<wchar_t> GetUSBDrives();

	// 获取占用指定驱动器的进程 ID 列表
	static std::vector<DWORD> GetProcessesUsingDrive(wchar_t driveLetter);

	// 终止指定进程，返回是否全部成功
	static bool TerminateProcesses(const std::vector<DWORD>& pids);

	// 弹出驱动器（先弹出介质，再尝试弹出设备）
	static bool EjectDrive(wchar_t driveLetter);

	// 获取进程可执行文件路径（可能为空）
	static std::wstring GetProcessImagePath(DWORD pid);

	// 安全弹出驱动器：如果有占用进程则询问用户（若 hWnd 非空），随后弹出
	static EjectError SafeEject(wchar_t driveLetter, HWND hWndForPrompt = nullptr);

	// 获取驱动器卷标（可能为空）
	static std::wstring GetVolumeLabel(wchar_t driveLetter);
private:
	// 内部辅助函数声明
	static STORAGE_BUS_TYPE GetBusTypeForDrive(wchar_t driveLetter);
	static bool IsPathOnDrive(const std::wstring& path, wchar_t driveLetter);
	static std::vector<DWORD> GetProcessesUsingDriveRM(const std::wstring& drivePath);
	static std::vector<DWORD> GetProcessesUsingDriveHandles(wchar_t driveLetter);
	static std::vector<DWORD> GetProcessesWithCwdOnDrive(wchar_t driveLetter);
	static bool FindDeviceInstanceForDrive(wchar_t driveLetter, DEVINST& outDevInst);
	static bool EjectMedia(wchar_t driveLetter);
	static bool EjectDeviceByInstance(DEVINST devInst);
};