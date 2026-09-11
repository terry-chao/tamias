#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// Ninja's msvc_deps_prefix is UTF-8. cl.exe /showIncludes follows the console
// code page, so force 65001 then re-exec the rest of the command line verbatim
// (argv-based spawn would drop quotes on -DFOO=\"C:/path\").

static wchar_t* skip_first_arg(wchar_t* cmd) {
  while (*cmd == L' ') {
    ++cmd;
  }
  if (*cmd == L'"') {
    ++cmd;
    while (*cmd && *cmd != L'"') {
      ++cmd;
    }
    if (*cmd == L'"') {
      ++cmd;
    }
  } else {
    while (*cmd && *cmd != L' ') {
      ++cmd;
    }
  }
  while (*cmd == L' ') {
    ++cmd;
  }
  return cmd;
}

int main(void) {
  SetConsoleOutputCP(65001);
  SetConsoleCP(65001);

  wchar_t* cmd = GetCommandLineW();
  if (!cmd) {
    return 1;
  }
  wchar_t* rest = skip_first_arg(cmd);
  if (!*rest) {
    return 1;
  }

  STARTUPINFOW si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  ZeroMemory(&pi, sizeof(pi));
  if (!CreateProcessW(NULL, rest, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
    return 1;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return (int)code;
}
