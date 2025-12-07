#include <windows.h>
#include <iostream>
#include <string>

// 共享内存结构体
struct SHARED_FLAG {
    bool protect;   // 保护标志
    bool updated;   // 更新标记
};

bool InjectBool(DWORD pid, const std::string& dllPath, bool flag)
{
    // 文件映射对象
    HANDLE hMap = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr,
        PAGE_READWRITE, 0, sizeof(SHARED_FLAG),
        "Global\\CapProtectBool");

    if (!hMap) {
        std::cerr << "CreateFileMapping failed, gle=" << GetLastError() << '\n';
        return false;
    }

    // 写入标记
    SHARED_FLAG* p = (SHARED_FLAG*)MapViewOfFile(hMap, FILE_MAP_WRITE, 0, 0, sizeof(SHARED_FLAG));
    if (!p) {
        std::cerr << "MapViewOfFile failed, gle=" << GetLastError() << '\n';
        CloseHandle(hMap);
        return false;
    }

    p->protect = flag;
    p->updated = true;  // 标记为已更新

    UnmapViewOfFile(p);

    // 注入
    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProc) {
        std::cerr << "OpenProcess failed, gle=" << GetLastError() << '\n';
        CloseHandle(hMap);
        return false;
    }

    size_t pathSize = dllPath.size() + 1;
    LPVOID pRemotePath = VirtualAllocEx(hProc, nullptr, pathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pRemotePath) {
        CloseHandle(hMap);
        CloseHandle(hProc);
        return false;
    }

    if (!WriteProcessMemory(hProc, pRemotePath, dllPath.c_str(), pathSize, nullptr)) {
        VirtualFreeEx(hProc, pRemotePath, 0, MEM_RELEASE);
        CloseHandle(hMap);
        CloseHandle(hProc);
        return false;
    }

    auto pLoadLibraryA = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA"));

    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, pLoadLibraryA, pRemotePath, 0, nullptr);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }

    VirtualFreeEx(hProc, pRemotePath, 0, MEM_RELEASE);

    CloseHandle(hProc);

    // 不关闭hMap，让DLL可以读取
    // CloseHandle(hMap); 

    return hThread != nullptr;
}

int main(int argc, char* argv[])
{
    if (argc < 3) {
        std::cout << "Usage: " << argv[0] << " <pid> <dll_path> [flag: 0/1]\n";
        return 1;
    }

    DWORD pid = std::strtoul(argv[1], nullptr, 10);
    std::string dllPath = argv[2];
    bool flag = (argc >= 4) ? (std::strtoul(argv[3], nullptr, 10) != 0) : true;

    if (InjectBool(pid, dllPath, flag))
        std::cout << "Injection successful\n";
    else
        std::cerr << "Injection failed\n";

    return 0;
}