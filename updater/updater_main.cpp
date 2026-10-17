/**
 * updater.exe — LogAnalyQt 独立更新器
 *
 * 纯 Win32 C++ 实现（不依赖 Qt），负责在主程序退出后完成文件替换。
 *
 * 命令行参数:
 *   --pid <PID>       主程序进程 ID（等待其退出）
 *   --source <path>   新版本文件路径（临时下载文件）
 *   --target <path>   要替换的目标文件路径（当前运行的 exe）
 *   --launch          替换成功后启动新程序
 *   --timeout <ms>    等待主程序退出的超时毫秒数（默认 30000）
 *
 * 执行流程:
 *   1. 等待主程序退出
 *   2. 备份旧文件 → target.bak
 *   3. 复制新文件 → target
 *   4. 成功 → 删除备份和临时文件 → 启动新程序
 *   5. 失败 → 恢复备份 → 弹出错误提示
 *   6. 权限不足 → 尝试 UAC 提升后重新执行
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string>
#include <sstream>

// 简单的宽字符字符串比较辅助
static bool streq(const wchar_t *a, const wchar_t *b) {
    return wcscmp(a, b) == 0;
}

// 显示错误消息框
static void showError(const std::wstring &msg) {
    MessageBoxW(NULL, msg.c_str(), L"LogAnalyQt 更新器", MB_OK | MB_ICONERROR);
}

// 显示信息消息框
static void showInfo(const std::wstring &msg) {
    MessageBoxW(NULL, msg.c_str(), L"LogAnalyQt 更新器",
                MB_OK | MB_ICONINFORMATION);
}

// 等待指定 PID 的进程退出
static bool waitForProcessExit(DWORD pid, DWORD timeoutMs) {
    HANDLE hProcess =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) {
        // 进程已不存在或无权访问，视为已退出
        return true;
    }

    DWORD result = WaitForSingleObject(hProcess, timeoutMs);
    CloseHandle(hProcess);
    return (result == WAIT_OBJECT_0);
}

// 尝试以管理员权限重新运行自身
static bool relaunchAsAdmin(const wchar_t *cmdLine) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.lpParameters = cmdLine;
    sei.nShow = SW_SHOWNORMAL;
    sei.fMask = SEE_MASK_NOASYNC;

    return ShellExecuteExW(&sei) != FALSE;
}

int WINAPI wWinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/,
                    LPWSTR lpCmdLine, int /*nShowCmd*/) {
    // 解析命令行参数
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        showError(L"命令行解析失败");
        return 1;
    }

    DWORD pid = 0;
    std::wstring source;
    std::wstring target;
    bool launch = false;
    DWORD timeoutMs = 30000;
    bool elevated = false; // 标记是否已经以管理员身份运行

    for (int i = 1; i < argc; ++i) {
        if (streq(argv[i], L"--pid") && i + 1 < argc) {
            pid = _wtoi(argv[++i]);
        } else if (streq(argv[i], L"--source") && i + 1 < argc) {
            source = argv[++i];
        } else if (streq(argv[i], L"--target") && i + 1 < argc) {
            target = argv[++i];
        } else if (streq(argv[i], L"--launch")) {
            launch = true;
        } else if (streq(argv[i], L"--timeout") && i + 1 < argc) {
            timeoutMs = _wtoi(argv[++i]);
        } else if (streq(argv[i], L"--elevated")) {
            elevated = true;
        }
    }

    LocalFree(argv);

    // 参数验证
    if (source.empty() || target.empty()) {
        showError(L"参数错误: 需要 --source 和 --target 参数");
        return 1;
    }

    // 1. 等待主程序退出
    if (pid > 0) {
        if (!waitForProcessExit(pid, timeoutMs)) {
            showError(L"等待主程序退出超时，更新取消。");
            return 1;
        }
        // 额外等待一小段时间确保文件锁释放
        Sleep(500);
    }

    // 2. 备份旧文件
    std::wstring backup = target + L".bak";
    // 如果已有旧备份，先删除
    DeleteFileW(backup.c_str());

    BOOL backupOk = MoveFileW(target.c_str(), backup.c_str());
    if (!backupOk) {
        DWORD err = GetLastError();

        // 如果是权限不足且尚未提升，尝试 UAC 提升
        if ((err == ERROR_ACCESS_DENIED) && !elevated) {
            // 重新构造完整命令行，附加 --elevated 标记
            std::wstring newCmd = std::wstring(lpCmdLine) + L" --elevated";
            if (relaunchAsAdmin(newCmd.c_str())) {
                return 0; // 提升后的进程会继续执行
            } else {
                showError(L"需要管理员权限来更新程序，但用户拒绝了提升请求。");
                return 1;
            }
        }

        // 其他错误：目标文件可能不存在（首次安装），尝试直接复制
        if (err != ERROR_FILE_NOT_FOUND) {
            std::wstringstream ss;
            ss << L"备份旧文件失败 (错误代码 " << err << L")。\n"
               << L"目标: " << target << L"\n"
               << L"请确保程序已关闭并且您有写入权限。";
            showError(ss.str());
            return 1;
        }
        // ERROR_FILE_NOT_FOUND: 目标不存在，可以直接复制
    }

    // 3. 复制新文件到目标位置
    BOOL copyOk = CopyFileW(source.c_str(), target.c_str(), FALSE);
    if (!copyOk) {
        DWORD err = GetLastError();
        std::wstringstream ss;
        ss << L"替换文件失败 (错误代码 " << err << L")。\n"
           << L"正在恢复备份...";

        // 恢复备份
        if (backupOk) {
            MoveFileW(backup.c_str(), target.c_str());
            ss << L"\n备份已恢复，程序未被修改。";
        }
        showError(ss.str());
        return 1;
    }

    // 4. 替换成功，清理
    // 删除备份文件
    DeleteFileW(backup.c_str());
    // 删除临时下载文件
    DeleteFileW(source.c_str());

    // 5. 启动新程序
    if (launch) {
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {};

        // 使用 CreateProcessW 启动新程序（以普通权限运行，即使 updater 以管理员身份运行）
        std::wstring cmdLine = L"\"" + target + L"\"";
        wchar_t *cmdBuf = new wchar_t[cmdLine.size() + 1];
        wcscpy(cmdBuf, cmdLine.c_str());

        BOOL createOk = CreateProcessW(target.c_str(), cmdBuf, NULL, NULL,
                                        FALSE, 0, NULL, NULL, &si, &pi);
        delete[] cmdBuf;

        if (createOk) {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        } else {
            // 启动失败，提示用户手动启动
            std::wstringstream ss;
            ss << L"更新成功，但启动新程序失败。\n"
               << L"请手动运行: " << target;
            showInfo(ss.str());
        }
    }

    return 0;
}
