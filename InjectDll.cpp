#include <string>
#include <vector>
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

// 常量定义（如果SDK版本不够新）
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

#ifndef WDA_MONITOR
#define WDA_MONITOR 0x00000001
#endif

#ifndef WDA_NONE
#define WDA_NONE 0x00000000
#endif

// 函数声明
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam);
void ApplyProtectionToWindow(HWND hwnd, bool enable);
bool IsMainWindow(HWND hwnd);
DWORD GetWindowProcessId(HWND hwnd);
BOOL IsAltTabWindow(HWND hwnd);

// 工作线程：循环检测并应用保护
DWORD WINAPI ProtectionThread(LPVOID) {
    bool lastProtectionState = false;

    while (g_running) {
        if (g_pFlag) {
            bool currentState = g_pFlag->protect;

            // 如果状态改变
            if (currentState != lastProtectionState || g_pFlag->updated) {
                lastProtectionState = currentState;
                g_pFlag->updated = false;

                // 获取当前进程ID（DLL所在的进程）
                DWORD currentProcessId = GetCurrentProcessId();

                // 枚举并保护当前进程的所有窗口
                EnumWindows(EnumWindowsProc, (LPARAM)currentProcessId);

                // 可选：显示状态通知
                if (currentState) {
                    OutputDebugStringA("窗口保护已启用\n");
                } else {
                    OutputDebugStringA("窗口保护已禁用\n");
                }
            }
        }

        Sleep(300); // 每300ms检查一次
    }

    return 0;
}

// 判断是否为主窗口
BOOL IsAltTabWindow(HWND hwnd) {
    // 检查窗口是否可见
    if (!IsWindowVisible(hwnd))
        return FALSE;

    // 检查是否有所有者
    HWND hwndWalk = GetWindow(hwnd, GW_OWNER);
    if (hwndWalk != NULL) {
        return FALSE;
    }

    // 检查窗口样式
    LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return FALSE;

    // 检查是否在任务栏显示
    HWND hwndRoot = GetAncestor(hwnd, GA_ROOTOWNER);
    HWND hwndPopup = GetLastActivePopup(hwndRoot);

    return hwndPopup == hwnd;
}

// 判断是否为主窗口
bool IsMainWindow(HWND hwnd) {
    return IsAltTabWindow(hwnd);
}

// 获取窗口所属进程ID
DWORD GetWindowProcessId(HWND hwnd) {
    DWORD processId = 0;
    GetWindowThreadProcessId(hwnd, &processId);
    return processId;
}

// 窗口枚举回调函数
BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    DWORD targetProcessId = (DWORD)lParam;
    DWORD windowProcessId = GetWindowProcessId(hwnd);

    // 检查是否为目标进程的窗口
    if (windowProcessId == targetProcessId) {
        // 只保护主窗口（也可以选择保护所有窗口）
        if (IsMainWindow(hwnd)) {
            bool enable = g_pFlag ? g_pFlag->protect : false;
            ApplyProtectionToWindow(hwnd, enable);
        }
    }

    return TRUE; // 继续枚举
}

// 对单个窗口应用保护
void ApplyProtectionToWindow(HWND hwnd, bool enable) {
    if (!IsWindow(hwnd))
        return;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        typedef BOOL(WINAPI * SetWindowDisplayAffinity_t)(HWND, DWORD);
        SetWindowDisplayAffinity_t pSetWindowDisplayAffinity =
            (SetWindowDisplayAffinity_t)GetProcAddress(hUser32, "SetWindowDisplayAffinity");

        if (pSetWindowDisplayAffinity) {
            if (enable) {
                // 阻止窗口被捕获
                pSetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
            } else {
                // 恢复默认行为
                pSetWindowDisplayAffinity(hwnd, WDA_NONE);
            }

            // 强制窗口重绘
            RedrawWindow(hwnd, NULL, NULL,
                         RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);

            return;
        }
    }
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

// 初始化函数
void InitializeProtection() {
    if (g_pFlag) {
        // 立即应用初始保护状态
        DWORD currentProcessId = GetCurrentProcessId();
        EnumWindows(EnumWindowsProc, (LPARAM)currentProcessId);
    }
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    UNREFERENCED_PARAMETER(lpvReserved);

    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        // 禁用线程库调用，提高性能
        DisableThreadLibraryCalls(hinstDLL);

        // 打开共享内存
        g_hMap = OpenFileMappingA(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, "Global\\CapProtectBool");
        if (g_hMap) {
            g_pFlag = (SHARED_FLAG *)MapViewOfFile(g_hMap, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(SHARED_FLAG));
            if (g_pFlag) {
                // 创建保护线程
                g_hThread = CreateThread(NULL, 0, ProtectionThread, NULL, 0, NULL);

                if (g_hThread) {
                    // 线程创建成功，立即初始化保护
                    InitializeProtection();
                } else {
                    // 线程创建失败，直接清理
                    if (g_pFlag) {
                        UnmapViewOfFile(g_pFlag);
                        g_pFlag = NULL;
                    }
                    CloseHandle(g_hMap);
                    g_hMap = NULL;
                }
            } else {
                CloseHandle(g_hMap);
                g_hMap = NULL;
            }
        }
        break;

    case DLL_PROCESS_DETACH:
        Cleanup();
        break;
    }
    return TRUE;
}

// 导出函数：手动触发保护
extern "C" __declspec(dllexport) void ToggleWindowProtection(bool enable) {
    if (g_pFlag) {
        g_pFlag->protect = enable;
        g_pFlag->updated = true;
    }
}