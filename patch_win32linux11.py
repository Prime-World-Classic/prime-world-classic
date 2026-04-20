path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/Win32_linux.h'
with open(path, 'r', encoding='utf-8') as f:
    text = f.read()

perf = """
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) {
    lpPerformanceCount->QuadPart = 1000000000LL;
    return TRUE;
}
"""
# Wait, typo in QueryPerformanceFrequency, should be lpFrequency
perf = """
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lpPerformanceCount->QuadPart = (LONGLONG)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency) {
    lpFrequency->QuadPart = 1000000000LL;
    return TRUE;
}
"""

text = text.replace('inline DWORD GetTickCount() {', perf + '\ninline DWORD GetTickCount() {')

with open(path, 'w', encoding='utf-8') as f:
    f.write(text)
