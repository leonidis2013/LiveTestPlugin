// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Runs a script with administrator rights (the VST3 folders are system folders):
//  - macOS: osascript "do shell script ... with administrator privileges" (password prompt)
//  - Windows: ShellExecuteEx "runas" on PowerShell (UAC prompt)
// Kept in its own file so <windows.h> never meets the JUCE headers.

#include "Elevate.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <shellapi.h>

int ltpRunElevatedScript (const std::wstring& scriptPath, std::string& error)
{
    std::wstring params = L"-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File \"" + scriptPath + L"\"";
    SHELLEXECUTEINFOW info {};
    info.cbSize = sizeof (info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = L"powershell.exe";
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    if (! ShellExecuteExW (&info) || info.hProcess == nullptr)
    {
        const DWORD e = GetLastError();
        error = e == ERROR_CANCELLED ? "Administrator permission was declined." : "Could not start PowerShell (error " + std::to_string ((unsigned long) e) + ").";
        return -1;
    }
    WaitForSingleObject (info.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess (info.hProcess, &code);
    CloseHandle (info.hProcess);
    if (code != 0) error = "The install script reported an error (code " + std::to_string ((unsigned long) code) + ").";
    return (int) code;
}

#else
 #include <cstdio>
 #include <sys/wait.h>
 #include <array>

static std::string shellQuote (const std::string& s)
{
    std::string out = "'";
    for (char c : s) { if (c == '\'') out += "'\\''"; else out += c; }
    return out + "'";
}

int ltpRunElevatedScript (const std::string& scriptPath, std::string& error)
{
    // AppleScript string: escape backslashes and double quotes
    std::string inner = "/bin/sh " + shellQuote (scriptPath);
    std::string escaped;
    for (char c : inner) { if (c == '\\' || c == '"') escaped += '\\'; escaped += c; }
    const std::string apple = "do shell script \"" + escaped + "\" with administrator privileges";
    const std::string cmd = "/usr/bin/osascript -e " + shellQuote (apple) + " 2>&1";

    FILE* pipe = popen (cmd.c_str(), "r");
    if (pipe == nullptr) { error = "Could not run osascript."; return -1; }
    std::array<char, 256> buf {};
    std::string output;
    while (fgets (buf.data(), (int) buf.size(), pipe) != nullptr) output += buf.data();
    const int status = pclose (pipe);
    const int code = WIFEXITED (status) ? WEXITSTATUS (status) : -1;
    if (code != 0)
        error = output.find ("-128") != std::string::npos ? "The administrator password prompt was cancelled." : "Install failed: " + output;
    return code;
}
#endif
