// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <string>

// Runs the given script elevated and waits for it. Returns the exit code (0 = success);
// on failure `error` holds a readable reason.
#if defined (_WIN32)
int ltpRunElevatedScript (const std::wstring& scriptPath, std::string& error);
#else
int ltpRunElevatedScript (const std::string& scriptPath, std::string& error);
#endif
