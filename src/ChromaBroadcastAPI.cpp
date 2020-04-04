#include <Windows.h>
#include <time.h>
#include <stdio.h>
#include <shlwapi.h>
#include <RzErrors.h>
#include <RzChromaBroadcastAPITypes.h>
#include "json.hpp"

using namespace RzChromaBroadcastAPI;

const wchar_t RZBROADCAST_EVENT[] = L"Global\\{91A407C5-C2B8-49FB-ABF9-88913B0B6ADD}";
const wchar_t RZBROADCAST_SHARED_MEMORY[] = L"Global\\{A66DE3D7-B9D1-4980-9A0E-BBB4AA943535}";
const wchar_t RZSYNAPSE3_MUTEX[] = L"Global\\{08B4F43A-DA51-4120-B388-CE0F8CE6F61A}";
const wchar_t RZSYNAPSE3_NAME[] = L"Razer Synapse Service";
const wchar_t RZBROADCAST_APP_NUM_EVENT[] = L"Global\\{08983A2E-6665-42B2-9492-38D5B1F3340A}";
const char RZBROADCAST_REG_SUBKEY[] = "Software\\Razer\\ChromaBroadcast";
const char RZBROADCAST_DAT_KEY[] = "h4cQkm3pL3a5E8u71FyoUc4Ntm34NsU5ukc";
const char RZBROADCAST_DEV_ENABLE[] = "{E69A5B35-5E42-42A0-8721-9F7279269950}";

#define RZLOGLEVEL_FATAL 0
#define RZLOGLEVEL_ERROR 1
#define RZLOGLEVEL_WARN 2
#define RZLOGLEVEL_INFO 3
#define RZLOGLEVEL_DEBUG 4

#define BROADCAST_SUCCESS 0
#define CHROMA_DEVICE_NOT_FOUND 1
#define SYNAPSE3_NOT_INSTALLED 2
#define SYNAPSE3_NOT_RUNNING 3
#define SYNAPSE3_NOT_ONLINE 4
#define BROADCAST_DISABLED 5
#define BROADCAST_APP_DISABLED 6
#define BROADCAST_MODULE_NOT_FOUND 7
#define BROADCAST_DATA_NULL 8
#define BROADCAST_DATA_INIT_SUCCESS 9

#pragma pack(push, 1)
struct RZEventData
{
	RZID index;
	CHROMA_BROADCAST_EFFECT effect;
	DWORD Reserved1;
	DWORD TickCount;
	DWORD Reserved3;
};

struct RZEventSharedMemoryData
{
	DWORD idx;
	DWORD Reserved;
	RZEventData events[10];
};
#pragma pack(pop)

struct RZEventSharedMemory
{
	HANDLE file;
	RZEventSharedMemoryData* mem;
};

FILE* logFile = nullptr;

void Log(unsigned char loglevel, const char *filename, int fileline, const char *format, ...)
{
	if (!logFile)
		return;

	if (loglevel > RZLOGLEVEL_DEBUG)
		loglevel = RZLOGLEVEL_DEBUG;

	va_list ArgList;
	va_start(ArgList, format);

	char line[260];
	vsprintf(line, format, ArgList);

	const char* levels[] = { "Fatal", "Error", "Warn", "Info", "Debug" };

	char time[100];
	GetDateFormatA(LOCALE_USER_DEFAULT, 0, NULL, "yyyy'-'MM'-'dd HH':'mm", time, sizeof(time));

	fprintf(logFile, "[%s][%s][%s:%d]%s\n", time, levels[loglevel], filename, fileline, line);

	va_end(ArgList);
}

RZSTATUS lastLogStatus = BROADCAST_SUCCESS;
void SetBroadcastLog(RZSTATUS value)
{
	if (lastLogStatus == value)
		return;

	switch (value)
	{
	case BROADCAST_SUCCESS: Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status BROADCAST_SUCCESS", __FUNCTION__); break;
	case CHROMA_DEVICE_NOT_FOUND: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status CHROMA_DEVICE_NOT_FOUND", __FUNCTION__); break;
	case SYNAPSE3_NOT_INSTALLED: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status SYNAPSE3_NOT_INSTALLED", __FUNCTION__); break;
	case SYNAPSE3_NOT_RUNNING: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status SYNAPSE3_NOT_RUNNING", __FUNCTION__); break;
	case SYNAPSE3_NOT_ONLINE: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status SYNAPSE3_NOT_ONLINE", __FUNCTION__); break;
	case BROADCAST_DISABLED: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status BROADCAST_DISABLED", __FUNCTION__); break;
	case BROADCAST_APP_DISABLED: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status BROADCAST_APP_DISABLED", __FUNCTION__); break;
	case BROADCAST_MODULE_NOT_FOUND: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status BROADCAST_MODULE_NOT_FOUND", __FUNCTION__); break;
	case BROADCAST_DATA_NULL: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status BROADCAST_DATA_NULL", "SetBroadcastLog"); break;
	case BROADCAST_DATA_INIT_SUCCESS: Log(RZLOGLEVEL_WARN, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns status BROADCAST_DATA_INIT_SUCCESS", __FUNCTION__); break;
	}

	lastLogStatus = value;
}

class CChromaBroadcastAPI
{
public:
	static bool IsInitialized;

private:
	static RZEVENTNOTIFICATIONCALLBACK NotificationCallback;
	static HANDLE UninitEvent;
	static HANDLE BroadcastDataThreadHandle;
	static HANDLE MonitorOnlineThreadHandle;
	static HANDLE BroadcastEventData;
	static int Index;
	static std::string Title;
	static CRITICAL_SECTION Critical;
	static bool Synapse3NotOnline;
	static bool Running;
	static RZSTATUS LogStatus;

	static void OpenEventSharedMemory(RZEventSharedMemory& esm)
	{
		esm.file = 0;
		esm.mem = 0;
		esm.file = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, RZBROADCAST_SHARED_MEMORY);
		if (esm.file)
		{
			esm.mem = (RZEventSharedMemoryData*)MapViewOfFile(esm.file, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RZEventSharedMemoryData));
		}
		else
		{
			esm.file = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(RZEventSharedMemoryData), RZBROADCAST_SHARED_MEMORY);
			if (esm.file)
			{
				esm.mem = (RZEventSharedMemoryData*)MapViewOfFile(esm.file, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(RZEventSharedMemoryData));
				if (esm.mem)
					memset(esm.mem, 0, sizeof(RZEventSharedMemoryData));
			}
		}
	}

	static bool CheckIsChromaBroadcastEnabled()
	{
		HKEY phkResult;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, RZBROADCAST_REG_SUBKEY, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &phkResult))
			return false;

		DWORD Enable = 0;
		DWORD EnableLen = sizeof(Enable);
		if (RegQueryValueExA(phkResult, "Enable", 0, 0, (LPBYTE)&Enable, &EnableLen))
		{
			RegCloseKey(phkResult);
			return false;
		}
		RegCloseKey(phkResult);

		return Enable != 0;
	}

	static bool CheckIsChromaBroadcastForAppEnabled()
	{
		HKEY phkResult;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, (std::string(RZBROADCAST_REG_SUBKEY) + "\\" + Title + ".exe").c_str(), 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &phkResult))
			return false;

		DWORD Enable = 0;
		DWORD EnableLen = sizeof(Enable);
		if (RegQueryValueExA(phkResult, "Enable", 0, 0, (LPBYTE)&Enable, &EnableLen))
		{
			RegCloseKey(phkResult);
			return false;
		}
		RegCloseKey(phkResult);

		return Enable != 0;
	}

	static DWORD WINAPI Thread_BroadcastData(LPVOID lpThreadParameter)
	{
		static __time64_t lastTime;
		RZEventSharedMemory shared;
		OpenEventSharedMemory(shared);

		HANDLE Handles[] = { BroadcastEventData, UninitEvent };
		if (!WaitForMultipleObjects(2, Handles, 0, INFINITE))
		{
			bool needSynapse3Check = false;
			bool OpenSynapse3MutexSuccess = false;
			bool DeviceFound = false;
			bool IsChromaBroadcastEnabled = false;
			bool IsChromaBroadcastForAppEnabled = false;
			while (TryEnterCriticalSection(&Critical))
			{
				ULONGLONG startTick = GetTickCount64();
				
				auto idx = shared.mem->idx;
				if (idx < 10)
				{
					RZEventData* event = &shared.mem->events[idx];
					__time64_t Time;

					CHROMA_BROADCAST_EFFECT effect;
					memcpy(&effect, &event->effect, sizeof(CHROMA_BROADCAST_EFFECT));

					if (!needSynapse3Check)
					{
						double diff =_difftime64(_time64(&Time), lastTime);
						if (!Running)
						{
							if (diff > 0.5)
								needSynapse3Check = true;
						}
						if (diff > 2.0)
						{
							needSynapse3Check = true;
						}
					}

					bool sendEvent = true;
					if (effect.IsAppSpecific == 1 && event->index)
					{
						if (Index != event->index)
							sendEvent = false;
					}

					if (needSynapse3Check)
					{
						OpenSynapse3MutexSuccess = false;
						DeviceFound = true;
						HANDLE Synapse3Mutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, RZSYNAPSE3_MUTEX);
						if (Synapse3Mutex)
						{
							ReleaseMutex(Synapse3Mutex);
							CloseHandle(Synapse3Mutex);
							OpenSynapse3MutexSuccess = true;
						}
						IsChromaBroadcastEnabled = CheckIsChromaBroadcastEnabled();
						IsChromaBroadcastForAppEnabled = CheckIsChromaBroadcastForAppEnabled();
						_time64(&lastTime);
						needSynapse3Check = 0;
					}

					if (!DeviceFound)
					{
						SetBroadcastLog(CHROMA_DEVICE_NOT_FOUND);
					}
					else if (OpenSynapse3MutexSuccess)
					{
						if (!IsChromaBroadcastEnabled)
						{
							SetBroadcastLog(BROADCAST_DISABLED);
						}
						else if (!IsChromaBroadcastForAppEnabled)
						{
							SetBroadcastLog(BROADCAST_APP_DISABLED);
						}
					}
					else if (Synapse3NotOnline == true)
					{
						SetBroadcastLog(SYNAPSE3_NOT_ONLINE);
					}

					if (NotificationCallback)
					{
						if (OpenSynapse3MutexSuccess && DeviceFound && IsChromaBroadcastEnabled && IsChromaBroadcastForAppEnabled)
						{
							if (sendEvent)
							{
								NotificationCallback(BROADCAST_EFFECT, &effect);
								if (!Running)
								{
									NotificationCallback(BROADCAST_STATUS, (PRZPARAM) LIVE);
									Running = true;
								}
								SetBroadcastLog(BROADCAST_SUCCESS);
							}
						}
						else if (Running)
						{
							NotificationCallback(BROADCAST_STATUS, (PRZPARAM) NOT_LIVE);
							Running = false;
						}
					}

					ULONGLONG endStartDiff = GetTickCount64() - startTick;
					if ((double)endStartDiff < 33.33333333333334)
						Sleep((unsigned int)(33.33333333333334 - (double)endStartDiff));
				}

				LeaveCriticalSection(&Critical);
			}
		}

		if (shared.mem)
			UnmapViewOfFile(shared.mem);
		if (shared.file)
			CloseHandle(shared.file);
		return 0;
	}

	static DWORD WINAPI Thread_MonitorOnline(LPVOID lpThreadParameter)
	{
		static __time64_t lastTime;
		
		__time64_t Time;
		if (WaitForSingleObject(UninitEvent, 1000))
		{
			do
			{
				if (_difftime64(_time64(&Time), lastTime) > 3.0)
				{
					_time64(&lastTime);

					SC_HANDLE hSCObject = OpenSCManagerW(NULL, NULL, SC_MANAGER_ENUMERATE_SERVICE);
					if (hSCObject)
					{
						bool state4 = false;
						SC_HANDLE service = OpenServiceW(hSCObject, RZSYNAPSE3_NAME, SC_MANAGER_ENUMERATE_SERVICE);
						if (service)
						{
							DWORD pcbBytesNeeded = 0;
							SERVICE_STATUS_PROCESS status;
							memset(&status, 0, sizeof(SERVICE_STATUS_PROCESS));
							if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (LPBYTE)&status, sizeof(SERVICE_STATUS_PROCESS), &pcbBytesNeeded) && status.dwCurrentState == SERVICE_RUNNING)
								state4 = true;
							CloseServiceHandle(service);
						}
						CloseServiceHandle(hSCObject);

						if (state4)
						{
							Synapse3NotOnline = true;
							HANDLE Synapse3Mutex = OpenMutexW(MUTEX_ALL_ACCESS, FALSE, RZSYNAPSE3_MUTEX);
							if (!Synapse3Mutex)
							{
								SetBroadcastLog(SYNAPSE3_NOT_ONLINE);
							}
							ReleaseMutex(Synapse3Mutex);
							CloseHandle(Synapse3Mutex);
							continue;
						}
					}
					
					Synapse3NotOnline = false;
					SetBroadcastLog(SYNAPSE3_NOT_RUNNING);

					if (NotificationCallback)
					{
						NotificationCallback(BROADCAST_STATUS, (PRZPARAM) NOT_LIVE);
						Running = false;
					}
				}
			} while (WaitForSingleObject(UninitEvent, 1000));
		}
		return 0;
	}

	static void RegisterApp()
	{
		std::string regKey = std::string(RZBROADCAST_REG_SUBKEY) + "\\" + Title + ".exe";

		HKEY phkResult;
		bool NewReg = true;
		if (!RegOpenKeyExA(HKEY_LOCAL_MACHINE, regKey.c_str(), 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &phkResult))
		{
			NewReg = false;
			RegCloseKey(phkResult);
		}

		HKEY hKey;
		if (!RegCreateKeyExA(HKEY_LOCAL_MACHINE, regKey.c_str(), 0, 0, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, 0, &hKey, 0))
		{
			RegSetValueExA(hKey, "Title", 0, REG_SZ, (LPBYTE)Title.c_str(), Title.size());
			char path[260];
			GetModuleFileNameA(0, path, sizeof(path));
			RegSetValueExA(hKey, "Path", 0, REG_SZ, (LPBYTE)path, strlen(path));
			if (NewReg)
			{
				DWORD one = 1;
				RegSetValueExW(hKey, L"Enable", 0, REG_DWORD, (LPBYTE)&one, sizeof(one));
			}
			RegSetValueExW(hKey, L"Index", 0, REG_DWORD, (LPBYTE)&Index, sizeof(Index));
			RegCloseKey(hKey);
		}
	}

	static int VerifyAppId(RZAPPID app)
	{
		int ret = -1;

		std::wstring guidWStr;
		guidWStr.resize(40);
		StringFromGUID2(app, guidWStr.data(), 39);
		guidWStr.resize(lstrlen(guidWStr.c_str()));
		std::string guidStr(guidWStr.begin(), guidWStr.end());

		HKEY phkResult;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, RZBROADCAST_REG_SUBKEY, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &phkResult))
			return -1;

		std::string DataPath;
		DataPath.resize(260);
		DWORD DataPathLen = 260;
		if (RegQueryValueExA(phkResult, "DataPath", 0, 0, (LPBYTE)DataPath.data(), &DataPathLen))
		{
			RegCloseKey(phkResult);
			return -1;
		}
		DataPath.resize(strlen(DataPath.c_str()));
		RegCloseKey(phkResult);

		FILE* f = fopen((DataPath + "\\broadcast.dat").c_str(), "rb");
		if (!f)
			return -1;

		DWORD len;
		fread(&len, 1, sizeof(len), f);
		char *data = new char[len+1];
		fread(data, 1, len, f);
		fclose(f);
		data[len] = 0;

		for (DWORD i = 0; i < len; i++)
			data[i] ^= RZBROADCAST_DAT_KEY[i % strlen(RZBROADCAST_DAT_KEY)];

		nlohmann::json jsn = nlohmann::json::parse(data);

		for (const auto &app : jsn["app"])
		{
			if (guidStr == app["guid"])
			{
				int Status = strtol(std::string(app["status"]).c_str(), NULL, 10);
				if (Status != 1 && Status != 2)
					continue;

				Index = strtol(std::string(app["index"]).c_str(), NULL, 10);
				Title = app["title"];

				RegisterApp();

				ret = Status == 2 ? (PathFileExistsA((DataPath + "\\" + RZBROADCAST_DEV_ENABLE).c_str()) ? 2 : 3) : 1;
				break;
			}
		}

		delete[]data;
		return ret;
	}

public:
	static RZRESULT Init(RZAPPID app)
	{
		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][START]%s", __FUNCTION__);

		HKEY phkResult;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, RZBROADCAST_REG_SUBKEY, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &phkResult))
		{
			Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Broadcast Module Not Installed", __FUNCTION__, RZRESULT_NOT_FOUND);
			return RZRESULT_NOT_FOUND;
		}
		RegCloseKey(phkResult);

		RZRESULT res = VerifyAppId(app);
		if (!res)
		{
			Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Unknown AppId", __FUNCTION__, RZRESULT_UNKNOWN_APPID);
			return RZRESULT_UNKNOWN_APPID;
		}

		switch (res)
		{
		case -1: Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Invalid AppId", __FUNCTION__, RZRESULT_ACCESS_DENIED); return RZRESULT_ACCESS_DENIED;
		case 3: Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Setup required for Test AppId", __FUNCTION__, RZRESULT_RESOURCE_DISABLED); return RZRESULT_RESOURCE_DISABLED;
		case 2: Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s Test AppId %d verified!", __FUNCTION__, Index); break;
		case 1: Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s AppId %d verified!", __FUNCTION__, Index); break;
		}

		HANDLE AppNumEvent = OpenEventW(EVENT_ALL_ACCESS, FALSE, RZBROADCAST_APP_NUM_EVENT);
		if (AppNumEvent)
		{
			PulseEvent(AppNumEvent);
			CloseHandle(AppNumEvent);
		}

		BroadcastEventData = CreateEventW(NULL, TRUE, FALSE, RZBROADCAST_EVENT);
		if (!BroadcastEventData)
		{
			res = GetLastError();
			if (res)
				Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Failed to create Broadcast Event Data", __FUNCTION__, res);
		}

		InitializeCriticalSection(&Critical);

		if (BroadcastEventData)
		{
			if (!BroadcastDataThreadHandle || BroadcastDataThreadHandle == INVALID_HANDLE_VALUE)
			{
				BroadcastDataThreadHandle = CreateThread(NULL, 0, Thread_BroadcastData, NULL, 0, NULL);
				if (!BroadcastDataThreadHandle)
				{
					res = GetLastError();
					if (res)
						Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Failed to create Broadcast Data thread", __FUNCTION__, res);
				}
				if (!MonitorOnlineThreadHandle || MonitorOnlineThreadHandle == INVALID_HANDLE_VALUE)
				{
					MonitorOnlineThreadHandle = CreateThread(NULL, 0, Thread_MonitorOnline, NULL, 0, NULL);
					if (!MonitorOnlineThreadHandle)
					{
						res = GetLastError();
						if (res)
							Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Failed to create Monitor Online thread", __FUNCTION__, res);
					}
				}
			}
			if (BroadcastEventData && BroadcastDataThreadHandle)
			{
				if (MonitorOnlineThreadHandle)
					res = 0;
			}
		}

		UninitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][END]%s", __FUNCTION__);
		return res;
	}

	static RZRESULT InitEx(int index, std::string title)
	{
		Index = index;
		Title = title;

		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][START]%s", __FUNCTION__);

		HKEY phkResult;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, RZBROADCAST_REG_SUBKEY, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &phkResult))
		{
			Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Broadcast Module Not Installed", __FUNCTION__, RZRESULT_NOT_FOUND);
			return RZRESULT_NOT_FOUND;
		}
		RegCloseKey(phkResult);

		RegisterApp();

		HANDLE AppNumEvent = OpenEventW(EVENT_ALL_ACCESS, FALSE, RZBROADCAST_APP_NUM_EVENT);
		if (AppNumEvent)
		{
			PulseEvent(AppNumEvent);
			CloseHandle(AppNumEvent);
		}

		RZRESULT res = RZRESULT_SUCCESS;
		BroadcastEventData = CreateEventW(NULL, TRUE, FALSE, RZBROADCAST_EVENT);
		if (!BroadcastEventData)
		{
			res = GetLastError();
			if (res)
				Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Failed to create Broadcast Event Data", __FUNCTION__, res);
		}

		InitializeCriticalSection(&Critical);

		if (BroadcastEventData)
		{
			if (!BroadcastDataThreadHandle || BroadcastDataThreadHandle == INVALID_HANDLE_VALUE)
			{
				BroadcastDataThreadHandle = CreateThread(NULL, 0, Thread_BroadcastData, NULL, 0, NULL);
				if (!BroadcastDataThreadHandle)
				{
					res = GetLastError();
					if (res)
						Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Failed to create Broadcast Data thread", __FUNCTION__, res);
				}
				if (!MonitorOnlineThreadHandle || MonitorOnlineThreadHandle == INVALID_HANDLE_VALUE)
				{
					MonitorOnlineThreadHandle = CreateThread(NULL, 0, Thread_MonitorOnline, NULL, 0, NULL);
					if (!MonitorOnlineThreadHandle)
					{
						res = GetLastError();
						if (res)
							Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Failed to create Monitor Online thread", __FUNCTION__, res);
					}
				}
			}
			if (BroadcastEventData && BroadcastDataThreadHandle)
			{
				if (MonitorOnlineThreadHandle)
					res = 0;
			}
		}

		UninitEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][END]%s", __FUNCTION__);
		return res;
	}

	static RZRESULT UnInit()
	{
		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][START]%s", __FUNCTION__);
		SetEvent(UninitEvent);

		if (BroadcastDataThreadHandle)
		{
			WaitForSingleObject(BroadcastDataThreadHandle, 1000);
			if (CloseHandle(BroadcastDataThreadHandle))
				BroadcastDataThreadHandle = INVALID_HANDLE_VALUE;
		}

		if (MonitorOnlineThreadHandle)
		{
			WaitForSingleObject(MonitorOnlineThreadHandle, 1000);
			if (CloseHandle(MonitorOnlineThreadHandle))
				MonitorOnlineThreadHandle = INVALID_HANDLE_VALUE;
		}

		if (CloseHandle(UninitEvent))
			UninitEvent = INVALID_HANDLE_VALUE;

		if (BroadcastEventData)
		{
			if (CloseHandle(BroadcastEventData))
				BroadcastEventData = INVALID_HANDLE_VALUE;
		}

		HANDLE AppNumEvent = OpenEventW(EVENT_ALL_ACCESS, 0, RZBROADCAST_APP_NUM_EVENT);
		if (AppNumEvent)
		{
			PulseEvent(AppNumEvent);
			CloseHandle(AppNumEvent);
		}

		DeleteCriticalSection(&Critical);
		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][END]%s", __FUNCTION__);
		return RZRESULT_SUCCESS;
	}

	static RZRESULT RegisterEventNotification(RZEVENTNOTIFICATIONCALLBACK callback)
	{
		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][START]%s", __FUNCTION__);

		if (!callback)
		{
			Log(RZLOGLEVEL_ERROR, __FILE__, __LINE__, "[ChromaBroadcastAPI]%s returns error code %d | Event Notification function is Null", __FUNCTION__, RZRESULT_INVALID_PARAMETER);
			return RZRESULT_INVALID_PARAMETER;
		}

		if (!NotificationCallback)
		{
			EnterCriticalSection(&Critical);
			NotificationCallback = callback;
			LeaveCriticalSection(&Critical);
		}

		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][END]%s", __FUNCTION__);
		return RZRESULT_SUCCESS;
	}

	static RZRESULT UnRegisterEventNotification()
	{
		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][START]%s", __FUNCTION__);

		if (NotificationCallback && TryEnterCriticalSection(&Critical))
		{
			NotificationCallback = nullptr;
			LeaveCriticalSection(&Critical);
		}

		Log(RZLOGLEVEL_INFO, __FILE__, __LINE__, "[ChromaBroadcastAPI][END]%s", __FUNCTION__);

		return RZRESULT_SUCCESS;
	}
};

bool CChromaBroadcastAPI::IsInitialized = false;
RZEVENTNOTIFICATIONCALLBACK CChromaBroadcastAPI::NotificationCallback = nullptr;
HANDLE CChromaBroadcastAPI::UninitEvent = INVALID_HANDLE_VALUE;
HANDLE CChromaBroadcastAPI::BroadcastDataThreadHandle = INVALID_HANDLE_VALUE;
HANDLE CChromaBroadcastAPI::MonitorOnlineThreadHandle = INVALID_HANDLE_VALUE;
HANDLE CChromaBroadcastAPI::BroadcastEventData = INVALID_HANDLE_VALUE;
int CChromaBroadcastAPI::Index = 0;
std::string CChromaBroadcastAPI::Title;
CRITICAL_SECTION CChromaBroadcastAPI::Critical;
bool CChromaBroadcastAPI::Synapse3NotOnline = false;
bool CChromaBroadcastAPI::Running = false;
RZSTATUS CChromaBroadcastAPI::LogStatus = 0;

extern "C" RZRESULT Init(RZAPPID app)
{
	if (CChromaBroadcastAPI::IsInitialized)
		return RZRESULT_ALREADY_INITIALIZED;
	CChromaBroadcastAPI::IsInitialized = true;
	return CChromaBroadcastAPI::Init(app);
}

extern "C" RZRESULT InitEx(int index, std::string title)
{
	if (CChromaBroadcastAPI::IsInitialized)
		return RZRESULT_ALREADY_INITIALIZED;
	CChromaBroadcastAPI::IsInitialized = true;
	return CChromaBroadcastAPI::InitEx(index, title);
}

extern "C" RZRESULT UnInit()
{
	if (!CChromaBroadcastAPI::IsInitialized)
		return RZRESULT_NOT_VALID_STATE;

	RZRESULT result = CChromaBroadcastAPI::UnInit();

	CChromaBroadcastAPI::IsInitialized = false;

	return result;
}

extern "C" RZRESULT RegisterEventNotification(RZEVENTNOTIFICATIONCALLBACK callback)
{
	if (!CChromaBroadcastAPI::IsInitialized)
		return RZRESULT_NOT_VALID_STATE;

	return CChromaBroadcastAPI::RegisterEventNotification(callback);
}

extern "C" RZRESULT UnRegisterEventNotification()
{
	if (!CChromaBroadcastAPI::IsInitialized)
		return RZRESULT_NOT_VALID_STATE;

	return CChromaBroadcastAPI::UnRegisterEventNotification();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	if (dwReason == DLL_PROCESS_ATTACH)
	{
		DisableThreadLibraryCalls(hModule);

		HKEY phkResult;
		if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, RZBROADCAST_REG_SUBKEY, 0, KEY_ALL_ACCESS | KEY_WOW64_32KEY, &phkResult))
			return TRUE;

		std::string InstallPath;
		InstallPath.resize(260);
		DWORD InstallPathLen = 260;
		if (RegQueryValueExA(phkResult, "InstallPath", 0, 0, (LPBYTE)InstallPath.data(), &InstallPathLen))
		{
			RegCloseKey(phkResult);
			return TRUE;
		}
		InstallPath.resize(strlen(InstallPath.c_str()));
		RegCloseKey(phkResult);
		InstallPath += "\\Logs\\";

		char Filename[260];
		if (!GetModuleFileNameA(0, Filename, sizeof(Filename)))
			return TRUE;

		PathStripPathA(Filename);
		PathRemoveExtensionA(Filename);
		PathAddExtensionA(Filename, ".log");
		InstallPath += Filename;

		logFile = fopen(InstallPath.c_str(), "a");
	}
	else
	{
		if (logFile)
		{
			fclose(logFile);
			logFile = nullptr;
		}
	}
	return TRUE;
}
