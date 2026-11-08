/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Desktop Melee.exe, the Windows package's launcher (package_windows.sh).
 *
 * melee.exe is melee-pc, a console program that is Desktop Melee only with
 * DESKTOP_MELEE_OVERLAY=1. This starts game\melee.exe, in the launcher's
 * folder, with that set, unless the player set it already, without a console
 * window, and passes its own arguments on. It does what LSEnvironment in
 * macOS's Info.plist and the AppImage's AppRun do on those systems. The game
 * and its DLLs are in a folder of their own so that the launcher is the only
 * program a player sees. */
#include <windows.h>

#include <stdlib.h>
#include <wchar.h>

static void fail(const wchar_t* what) {
    wchar_t text[512];
    swprintf(text, sizeof text / sizeof text[0], L"%ls (error %lu).", what, GetLastError());
    MessageBoxW(NULL, text, L"Desktop Melee", MB_OK | MB_ICONERROR);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR args, int show) {
    (void)instance;
    (void)previous;
    (void)show;

    /* The folder this launcher is in, then the game's folder in it. */
    static wchar_t dir[32768];
    const DWORD length = GetModuleFileNameW(NULL, dir, sizeof dir / sizeof dir[0]);
    if (length == 0 || length >= sizeof dir / sizeof dir[0]) {
        fail(L"Desktop Melee couldn't find its own folder");
        return 1;
    }
    wchar_t* slash = wcsrchr(dir, L'\\');
    if (slash == NULL) {
        fail(L"Desktop Melee couldn't find its own folder");
        return 1;
    }
    *slash = L'\0';
    if (wcslen(dir) + 32 >= sizeof dir / sizeof dir[0]) {
        fail(L"Desktop Melee's folder path is too long");
        return 1;
    }
    wcscat(dir, L"\\game");

    static wchar_t exe[32768];
    swprintf(exe, sizeof exe / sizeof exe[0], L"%ls\\melee.exe", dir);

    /* CreateProcessW may write to the command line, so it is a copy. */
    const size_t size = wcslen(exe) + wcslen(args) + 4;
    wchar_t* command = malloc(size * sizeof *command);
    if (command == NULL) {
        return 1;
    }
    swprintf(command, size, L"\"%ls\"%ls%ls", exe, args[0] != L'\0' ? L" " : L"", args);

    if (GetEnvironmentVariableW(L"DESKTOP_MELEE_OVERLAY", NULL, 0) == 0) {
        SetEnvironmentVariableW(L"DESKTOP_MELEE_OVERLAY", L"1");
    }

    STARTUPINFOW startup = {.cb = sizeof startup};
    PROCESS_INFORMATION process;
    if (!CreateProcessW(
            exe, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, dir, &startup, &process))
    {
        fail(L"Desktop Melee couldn't start game\\melee.exe, which should be in its folder");
        free(command);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    free(command);
    return 0;
}
