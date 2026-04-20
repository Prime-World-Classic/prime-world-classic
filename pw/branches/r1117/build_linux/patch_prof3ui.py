import re

path_prof = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/InlineProfiler3/Profiler3UIData.cpp"
with open(path_prof, "r", encoding="cp1251") as f:
    text_prof = f.read()

text_prof = re.sub(
    r'(void SamplingData::UpdateThreadsTimes\(\)\n\{)([\s\S]*?)(\n\})',
    r'\1\n#ifdef _WIN32\n\2\n#endif\3',
    text_prof
)

text_prof = re.sub(
    r'(void SamplingData::UpdateMemoryUsage\(\)\n\{)([\s\S]*?)(\n\})',
    r'\1\n#ifdef _WIN32\n\2\n#else\n  memoryUsage = 0;\n#endif\3',
    text_prof
)

with open(path_prof, "w", encoding="cp1251") as f:
    f.write(text_prof)
