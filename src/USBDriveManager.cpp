#include "WinPch.h"

#include "USBDriveManager.h"
#include <RestartManager.h>
#include <SetupAPI.h>
#include <devguid.h>
#include <cfgmgr32.h>
#include <winternl.h>
#include <ntstatus.h>
#include <TlHelp32.h>
#include <Psapi.h>
#include <algorithm>
#include <ranges>
#include <memory>

#pragma comment(lib, "Rstrtmgr.lib")
#pragma comment(lib, "Setupapi.lib")
#pragma comment(lib, "Cfgmgr32.lib")
#pragma comment(lib, "ntdll.lib")

namespace {
	// RAII 封装 Windows 句柄
	struct HandleDeleter {
		void operator()(HANDLE h) const { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); }
	};
	using UniqueHandle = std::unique_ptr<void, HandleDeleter>;

	// 将盘符转换为 "\\.\X:" 路径
	std::wstring VolumePath(wchar_t driveLetter) {
		return std::format(L"\\\\.\\{}:", driveLetter);
	}


}

// 判断路径是否在指定驱动器上（忽略大小写）
bool USBDriveManager::IsPathOnDrive(const std::wstring& path, wchar_t driveLetter) {
	if (path.size() < 2) return false;
	wchar_t first = towupper(path[0]);
	wchar_t target = towupper(driveLetter);
	return (first == target && path[1] == L':');
}

std::vector<wchar_t> USBDriveManager::GetUSBDrives() {
	std::vector<wchar_t> usbDrives;
	DWORD drives = GetLogicalDrives();
	for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
		if (drives & (1 << (letter - L'A'))) {
			if (GetBusTypeForDrive(letter) == BusTypeUsb)
				usbDrives.push_back(letter);
		}
	}
	return usbDrives;
}

std::vector<DWORD> USBDriveManager::GetProcessesUsingDrive(wchar_t driveLetter) {
	std::wstring drivePath = std::format(L"{}:\\", driveLetter);
	auto p1 = GetProcessesUsingDriveRM(drivePath);
	auto p2 = GetProcessesUsingDriveHandles(driveLetter);
	auto p3 = GetProcessesWithCwdOnDrive(driveLetter);

	// 合并并去重
	std::vector<DWORD> all;
	all.insert(all.end(), p1.begin(), p1.end());
	all.insert(all.end(), p2.begin(), p2.end());
	all.insert(all.end(), p3.begin(), p3.end());
	std::ranges::sort(all);
	auto [first, last] = std::ranges::unique(all);
	all.erase(first, last);
	return all;
}

bool USBDriveManager::TerminateProcesses(const std::vector<DWORD>& pids) {
	bool allOk = true;
	for (DWORD pid : pids) {
		UniqueHandle hProcess(OpenProcess(PROCESS_TERMINATE, FALSE, pid));
		if (!hProcess) {
			allOk = false;
			continue;
		}
		if (!TerminateProcess(hProcess.get(), 1))
			allOk = false;
	}
	return allOk;
}

bool USBDriveManager::EjectDrive(wchar_t driveLetter) {
	DEVINST devInst = 0;
	bool hasDevInst = FindDeviceInstanceForDrive(driveLetter, devInst);

	bool mediaOk = EjectMedia(driveLetter);
	bool deviceOk = false;
	if (hasDevInst)
		deviceOk = EjectDeviceByInstance(devInst);

	return mediaOk || deviceOk;
}

std::wstring USBDriveManager::GetProcessImagePath(DWORD pid) {
	std::wstring imagePath;
	UniqueHandle hProcess(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid));
	if (!hProcess)
		return imagePath;

	WCHAR path[MAX_PATH] = L"";
	DWORD size = MAX_PATH;
	if (QueryFullProcessImageNameW(hProcess.get(), 0, path, &size))
		imagePath = path;
	return imagePath;
}

USBDriveManager::EjectError USBDriveManager::SafeEject(wchar_t driveLetter, HWND hWndForPrompt) {
	auto pids = GetProcessesUsingDrive(driveLetter);

	if (!pids.empty()) {
		std::wstring procList;
		for (size_t i = 0; i < pids.size(); ++i) {
			std::wstring path = GetProcessImagePath(pids[i]);
			procList += std::format(L"PID {} - {}\n", pids[i], path.empty() ? L"<未知>" : path);
		}

		std::wstring msg = std::format(L"驱动器 {}: 上有以下进程占用，是否终止这些进程并弹出？\n\n{}",
			driveLetter, procList);
		int ret = MessageBoxW(hWndForPrompt, msg.c_str(), L"发现占用进程", MB_YESNO | MB_ICONQUESTION);
		if (ret != IDYES)
			return EjectError::UserCancel;

		if (!TerminateProcesses(pids)) {
			// 部分进程终止失败，但继续尝试弹出
			// 实际可以根据需求决定是否返回错误
		}
		Sleep(500); // 等待系统释放句柄
	}

	bool success = EjectDrive(driveLetter);
	return success ? EjectError::None : EjectError::EjectFailed;
}

std::wstring USBDriveManager::GetVolumeLabel(wchar_t driveLetter) {
	std::wstring rootPath = std::format(L"{}:\\", driveLetter);
	WCHAR volumeName[MAX_PATH + 1] = { 0 };
	WCHAR fileSystemName[MAX_PATH + 1] = { 0 };
	DWORD serialNumber = 0, maxComponentLen = 0, fileSystemFlags = 0;

	if (GetVolumeInformationW(rootPath.c_str(),
		volumeName, MAX_PATH + 1,
		&serialNumber,
		&maxComponentLen,
		&fileSystemFlags,
		fileSystemName, MAX_PATH + 1)) {
		return std::wstring(volumeName);
	}
	return L"";
}

STORAGE_BUS_TYPE USBDriveManager::GetBusTypeForDrive(wchar_t driveLetter) {
	std::wstring volPath = VolumePath(driveLetter);
	UniqueHandle hDevice(CreateFileW(volPath.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
	if (!hDevice)
		return BusTypeUnknown;

	STORAGE_PROPERTY_QUERY query{};
	query.PropertyId = StorageDeviceProperty;
	query.QueryType = PropertyStandardQuery;
	std::vector<BYTE> buffer(sizeof(STORAGE_DEVICE_DESCRIPTOR) + 512);
	DWORD bytesReturned = 0;
	BOOL result = DeviceIoControl(hDevice.get(), IOCTL_STORAGE_QUERY_PROPERTY,
		&query, sizeof(query), buffer.data(), (DWORD)buffer.size(), &bytesReturned, nullptr);

	if (!result || bytesReturned < sizeof(STORAGE_DEVICE_DESCRIPTOR))
		return BusTypeUnknown;
	auto* desc = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer.data());
	return desc->BusType;
}

std::vector<DWORD> USBDriveManager::GetProcessesUsingDriveRM(const std::wstring& drivePath) {
	std::vector<DWORD> pids;
	DWORD session = 0;
	WCHAR key[CCH_RM_SESSION_KEY + 1] = { 0 };
	if (RmStartSession(&session, 0, key) != ERROR_SUCCESS)
		return pids;

	PCWSTR resources[] = { drivePath.c_str() };
	if (RmRegisterResources(session, 1, resources, 0, nullptr, 0, nullptr) != ERROR_SUCCESS) {
		RmEndSession(session);
		return pids;
	}

	UINT needed = 0, count = 0;
	RM_PROCESS_INFO* info = nullptr;
	DWORD reason = 0;
	DWORD err = RmGetList(session, &needed, &count, nullptr, &reason);
	if (err == ERROR_MORE_DATA) {
		info = new RM_PROCESS_INFO[needed];
		count = needed;
		err = RmGetList(session, &needed, &count, info, &reason);
	}
	if (err == ERROR_SUCCESS) {
		for (UINT i = 0; i < count; ++i)
			pids.push_back(info[i].Process.dwProcessId);
	}
	delete[] info;
	RmEndSession(session);
	return pids;
}

// 句柄扫描相关结构体与函数（保持原样，略作整理）
namespace {
	typedef struct _SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX {
		PVOID Object;
		ULONG_PTR UniqueProcessId;
		ULONG_PTR HandleValue;
		ULONG GrantedAccess;
		USHORT CreatorBackTraceIndex;
		USHORT ObjectTypeIndex;
		ULONG HandleAttributes;
		ULONG Reserved;
	} SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX;

	typedef struct _SYSTEM_HANDLE_INFORMATION_EX {
		ULONG_PTR NumberOfHandles;
		ULONG_PTR Reserved;
		SYSTEM_HANDLE_TABLE_ENTRY_INFO_EX Handles[1];
	} SYSTEM_HANDLE_INFORMATION_EX;

	typedef NTSTATUS(NTAPI* pNtQuerySystemInformation)(ULONG, PVOID, ULONG, PULONG);

	pNtQuerySystemInformation GetNtQuerySystemInformation() {
		static pNtQuerySystemInformation fn = nullptr;
		if (!fn) {
			HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
			if (ntdll)
				fn = (pNtQuerySystemInformation)GetProcAddress(ntdll, "NtQuerySystemInformation");
		}
		return fn;
	}
}

std::vector<DWORD> USBDriveManager::GetProcessesUsingDriveHandles(wchar_t driveLetter) {
	std::vector<DWORD> pids;
	auto ntQuery = GetNtQuerySystemInformation();
	if (!ntQuery)
		return pids;

	ULONG bufSize = 0;
	NTSTATUS st = ntQuery(64, nullptr, 0, &bufSize);
	if (st != STATUS_INFO_LENGTH_MISMATCH)
		return pids;

	std::vector<BYTE> buf(bufSize);
	auto* info = reinterpret_cast<SYSTEM_HANDLE_INFORMATION_EX*>(buf.data());
	st = ntQuery(64, info, bufSize, &bufSize);
	if (st != STATUS_SUCCESS)
		return pids;

	for (ULONG_PTR i = 0; i < info->NumberOfHandles; ++i) {
		const auto& h = info->Handles[i];
		UniqueHandle hProc(OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)h.UniqueProcessId));
		if (!hProc)
			continue;

		HANDLE hDup = nullptr;
		if (DuplicateHandle(hProc.get(), (HANDLE)h.HandleValue, GetCurrentProcess(), &hDup,
			0, FALSE, DUPLICATE_SAME_ACCESS)) {
			UniqueHandle dupGuard(hDup);
			if (GetFileType(hDup) == FILE_TYPE_DISK) {
				WCHAR path[MAX_PATH * 2] = { 0 };
				DWORD len = GetFinalPathNameByHandleW(hDup, path, MAX_PATH * 2, FILE_NAME_NORMALIZED);
				if (len > 0 && len < MAX_PATH * 2) {
					std::wstring p(path);
					if (p.starts_with(L"\\\\?\\"))
						p = p.substr(4);
					if (IsPathOnDrive(p, driveLetter))
						pids.push_back((DWORD)h.UniqueProcessId);
				}
			}
		}
	}

	std::ranges::sort(pids);
	auto [first, last] = std::ranges::unique(pids);
	pids.erase(first, last);
	return pids;
}

// 通过 PEB 获取当前工作目录
namespace {
	typedef struct _UNICODE_STRING_T {
		USHORT Length;
		USHORT MaximumLength;
		PWSTR  Buffer;
	} UNICODE_STRING_T;

	// 偏移量在不同 Windows 版本可能不同，此处保持原始值
	constexpr size_t CURRENT_DIR_OFFSET = 0x38;

	typedef struct _PEB_T {
		BYTE Reserved1[2];
		BYTE BeingDebugged;
		BYTE Reserved2[1];
		PVOID Reserved3[2];
		PVOID Ldr;
		PVOID ProcessParameters;
	} PEB_T;

	typedef struct _PROCESS_BASIC_INFORMATION_T {
		NTSTATUS ExitStatus;
		PVOID PebBaseAddress;
		ULONG_PTR AffinityMask;
		LONG BasePriority;
		ULONG_PTR UniqueProcessId;
		ULONG_PTR InheritedFromUniqueProcessId;
	} PROCESS_BASIC_INFORMATION_T;

	typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(HANDLE, ULONG, PVOID, ULONG, PULONG);

	pNtQueryInformationProcess GetNtQueryInformationProcess() {
		static pNtQueryInformationProcess fn = nullptr;
		if (!fn) {
			HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
			if (ntdll)
				fn = (pNtQueryInformationProcess)GetProcAddress(ntdll, "NtQueryInformationProcess");
		}
		return fn;
	}
}

std::vector<DWORD> USBDriveManager::GetProcessesWithCwdOnDrive(wchar_t driveLetter) {
	std::vector<DWORD> pids;
	auto ntQueryInfoProc = GetNtQueryInformationProcess();
	if (!ntQueryInfoProc)
		return pids;

	DWORD procs[4096], needed;
	if (!EnumProcesses(procs, sizeof(procs), &needed))
		return pids;
	DWORD count = needed / sizeof(DWORD);

	for (DWORD i = 0; i < count; ++i) {
		DWORD pid = procs[i];
		if (pid == 0)
			continue;

		UniqueHandle hProc(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid));
		if (!hProc)
			continue;

		PROCESS_BASIC_INFORMATION_T pbi{};
		ULONG retLen = 0;
		NTSTATUS st = ntQueryInfoProc(hProc.get(), 0, &pbi, sizeof(pbi), &retLen);
		if (st != STATUS_SUCCESS || !pbi.PebBaseAddress)
			continue;

		PEB_T peb{};
		SIZE_T read = 0;
		if (!ReadProcessMemory(hProc.get(), pbi.PebBaseAddress, &peb, sizeof(peb), &read) ||
			read != sizeof(peb) || !peb.ProcessParameters)
			continue;

		UNICODE_STRING_T curDir{};
		if (!ReadProcessMemory(hProc.get(),
			reinterpret_cast<BYTE*>(peb.ProcessParameters) + CURRENT_DIR_OFFSET,
			&curDir, sizeof(curDir), &read) ||
			read != sizeof(curDir) || curDir.Length == 0 || !curDir.Buffer)
			continue;

		std::wstring dir(curDir.Length / sizeof(wchar_t), L'\0');
		if (!ReadProcessMemory(hProc.get(), curDir.Buffer, dir.data(), curDir.Length, &read) ||
			read != curDir.Length)
			continue;

		if (IsPathOnDrive(dir, driveLetter))
			pids.push_back(pid);
	}

	std::ranges::sort(pids);
	auto [first, last] = std::ranges::unique(pids);
	pids.erase(first, last);
	return pids;
}

bool USBDriveManager::FindDeviceInstanceForDrive(wchar_t driveLetter, DEVINST& outDevInst) {
	std::wstring volPath = VolumePath(driveLetter);
	UniqueHandle hVol(CreateFileW(volPath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, 0, nullptr));
	if (!hVol)
		return false;

	STORAGE_DEVICE_NUMBER devNum{};
	DWORD bytes = 0;
	if (!DeviceIoControl(hVol.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
		&devNum, sizeof(devNum), &bytes, nullptr))
		return false;

	HDEVINFO devInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_DISK, nullptr, nullptr,
		DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (devInfo == INVALID_HANDLE_VALUE)
		return false;

	bool found = false;
	SP_DEVICE_INTERFACE_DATA ifData{};
	ifData.cbSize = sizeof(ifData);
	for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_DISK, i, &ifData); ++i) {
		DWORD reqSize = 0;
		SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &reqSize, nullptr);
		if (!reqSize)
			continue;

		std::vector<BYTE> buffer(reqSize);
		auto* detail = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(buffer.data());
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

		if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize, nullptr, nullptr))
			continue;

		UniqueHandle hDisk(CreateFileW(detail->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
			nullptr, OPEN_EXISTING, 0, nullptr));
		if (!hDisk)
			continue;

		STORAGE_DEVICE_NUMBER diskNum{};
		if (DeviceIoControl(hDisk.get(), IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0,
			&diskNum, sizeof(diskNum), &bytes, nullptr) &&
			diskNum.DeviceNumber == devNum.DeviceNumber &&
			diskNum.DeviceType == devNum.DeviceType) {
			SP_DEVINFO_DATA devInfoData{};
			devInfoData.cbSize = sizeof(devInfoData);
			if (SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, detail, reqSize,
				nullptr, &devInfoData)) {
				outDevInst = devInfoData.DevInst;
				found = true;
				break;
			}
		}
	}

	SetupDiDestroyDeviceInfoList(devInfo);
	return found;
}

bool USBDriveManager::EjectMedia(wchar_t driveLetter) {
	std::wstring volPath = VolumePath(driveLetter);
	UniqueHandle hDevice(CreateFileW(volPath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr, OPEN_EXISTING, 0, nullptr));
	if (!hDevice)
		return false;

	DWORD returned = 0;
	BOOL result = DeviceIoControl(hDevice.get(),
		IOCTL_STORAGE_EJECT_MEDIA,
		nullptr, 0, nullptr, 0, &returned, nullptr);
	return result != FALSE;
}

bool USBDriveManager::EjectDeviceByInstance(DEVINST devInst) {
	bool ejected = false;
	DEVINST current = devInst;
	for (int level = 0; level < 5 && !ejected; ++level) {
		PNP_VETO_TYPE vetoType = PNP_VetoTypeUnknown;
		WCHAR vetoName[MAX_PATH] = { 0 };
		CONFIGRET cr = CM_Request_Device_EjectW(current, &vetoType, vetoName, MAX_PATH, 0);
		if (cr == CR_SUCCESS) {
			ejected = true;
			break;
		}
		DEVINST parent = 0;
		if (CM_Get_Parent(&parent, current, 0) != CR_SUCCESS)
			break;
		current = parent;
	}
	return ejected;
}