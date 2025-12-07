#include <string>
#include <windows.h>

struct SHARED_FLAG {
    bool protect; // 保护标志
    bool updated; // 更新标记
};

// 全局变量
HANDLE g_hMap = NULL;
SHARED_FLAG *g_pFlag = NULL;
HANDLE g_hThread = NULL;
bool g_running = true;

// 工作线程
DWORD WINAPI MonitorThread(LPVOID) {
    bool lastFlag = false;
    bool firstRun = true;

    while (g_running) {
        if (g_pFlag) {
            if (firstRun || g_pFlag->updated) {
                lastFlag = g_pFlag->protect;
                g_pFlag->updated = false;
                firstRun = false;

                const char *msg = lastFlag ? "DLL received TRUE - privacy ON" : "DLL received FALSE - privacy OFF";

                MessageBoxA(NULL, msg, "CaptureProtect DLL", MB_OK | MB_TOPMOST);
            }
        }

        Sleep(100); // 每100ms检查一次
    }

    return 0;
}

// 清理函数
extern "C" __declspec(dllexport) void Cleanup() {
    g_running = false;
    if (g_hThread) {
        WaitForSingleObject(g_hThread, 1000);
        CloseHandle(g_hThread);
        g_hThread = NULL;
    }
    if (g_pFlag) {
        UnmapViewOfFile(g_pFlag);
        g_pFlag = NULL;
    }
    if (g_hMap) {
        CloseHandle(g_hMap);
        g_hMap = NULL;
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        // 只执行一次初始化
        g_hMap = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, "Global\\CapProtectBool");
        if (g_hMap) {
            g_pFlag = (SHARED_FLAG *)MapViewOfFile(g_hMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SHARED_FLAG));
            if (g_pFlag) {
                // 创建监控线程
                g_hThread = CreateThread(NULL, 0, MonitorThread, NULL, 0, NULL);
            }
        }
        break;

    case DLL_PROCESS_DETACH:
        Cleanup();
        break;
    }
    return TRUE;
}