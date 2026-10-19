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
#include <cstdlib>

// 简单的宽字符字符串比较辅助
static bool streq(const wchar_t *a, const wchar_t *b) {
    return wcscmp(a, b) == 0;
}

// 显示错误消息框
static void showError(const std::wstring &msg) {
#ifdef LOGANALYQT_UPDATER_TESTING
    fwprintf(stderr, L"%ls\n", msg.c_str());
#else
    MessageBoxW(NULL, msg.c_str(), L"通联工坊 更新器", MB_OK | MB_ICONERROR);
#endif
}

// 显示信息消息框
static void showInfo(const std::wstring &msg) {
    MessageBoxW(NULL, msg.c_str(), L"通联工坊 更新器",
                MB_OK | MB_ICONINFORMATION);
}

// 等待指定 PID 的进程退出
static bool waitForProcessExit(DWORD pid, DWORD timeoutMs) {
    HANDLE hProcess =
        OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess == NULL) {
        return GetLastError() == ERROR_INVALID_PARAMETER;
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

    if (source == target || GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        showError(L"更新目标不存在或与更新包路径相同");
        return 1;
    }
    const std::wstring staging = target + L".update-" + std::to_wstring(GetCurrentProcessId());
    const std::wstring backup = target + L".backup-" + std::to_wstring(GetCurrentProcessId());
    if (!CopyFileW(source.c_str(), staging.c_str(), TRUE)) {
        const DWORD stagingError = GetLastError();
        DeleteFileW(staging.c_str());
#ifndef LOGANALYQT_UPDATER_TESTING
        if (stagingError == ERROR_ACCESS_DENIED && !elevated &&
            relaunchAsAdmin((std::wstring(lpCmdLine) + L" --elevated").c_str()))
            return 0;
#endif
        showError(L"无法在安装目录暂存新版本，旧程序保持不变。请检查空间和写入权限。");
        return 1;
    }

    // 1. 等待主程序退出
    if (pid > 0) {
        if (!waitForProcessExit(pid, timeoutMs)) {
            DeleteFileW(staging.c_str());
            showError(L"等待主程序退出超时，更新取消。");
            return 1;
        }
        // 额外等待一小段时间确保文件锁释放
        Sleep(500);
    }

    // 2. 备份旧文件
    BOOL backupOk = MoveFileW(target.c_str(), backup.c_str());
    if (!backupOk) {
        DWORD err = GetLastError();
        DeleteFileW(staging.c_str());

        // 如果是权限不足且尚未提升，尝试 UAC 提升
        if ((err == ERROR_ACCESS_DENIED) && !elevated) {
#ifndef LOGANALYQT_UPDATER_TESTING
            // 重新构造完整命令行，附加 --elevated 标记
            std::wstring newCmd = std::wstring(lpCmdLine) + L" --elevated";
            if (relaunchAsAdmin(newCmd.c_str())) {
                return 0; // 提升后的进程会继续执行
            } else {
                showError(L"需要管理员权限来更新程序，但用户拒绝了提升请求。");
                return 1;
            }
#else
            showError(L"备份旧程序失败，旧程序保持不变");
            return 1;
#endif
        }

        std::wstringstream ss;
        ss << L"备份旧文件失败 (错误代码 " << err << L")。\n"
           << L"目标: " << target << L"\n"
           << L"请确保程序已关闭并且您有写入权限。";
        showError(ss.str());
        return 1;
    }

    // 3. 复制新文件到目标位置
    BOOL copyOk;
#ifdef LOGANALYQT_UPDATER_TESTING
    if (std::getenv("LOGANALYQT_TEST_REPLACE_FAIL")) {
        SetLastError(ERROR_DISK_FULL);
        copyOk = FALSE;
    } else
#endif
    copyOk = MoveFileExW(staging.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
    if (!copyOk) {
        DWORD err = GetLastError();
        std::wstringstream ss;
        ss << L"替换文件失败 (错误代码 " << err << L")。\n"
           << L"正在恢复备份...";

        // 恢复备份
        if (backupOk) {
            if (MoveFileW(backup.c_str(), target.c_str()))
                ss << L"\n备份已恢复，程序未被修改。";
            else
                ss << L"\n自动恢复失败，请保留并手动恢复备份: " << backup;
        }
        DeleteFileW(staging.c_str());
        showError(ss.str());
        return 1;
    }

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
               << L"请手动运行: " << target << L"\n旧程序备份: " << backup;
            showInfo(ss.str());
            return 1;
        }
    }

    DeleteFileW(backup.c_str());
    DeleteFileW(source.c_str());
    return 0;
}
