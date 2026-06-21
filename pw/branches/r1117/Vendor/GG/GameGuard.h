#pragma once

extern "C" {
    __declspec(dllexport) bool windows_hacker_detect();
    __declspec(dllexport) bool process_hacker_detect();
    __declspec(dllexport) bool inject_dll_detect();
}