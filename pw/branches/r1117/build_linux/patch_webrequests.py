import re

h_path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Shared/WebRequests.h"
with open(h_path, "r", encoding="cp1251") as f:
    h_text = f.read()

h_text = re.sub(
    r'#include <Windows.h>\n#include <Wininet.h>',
    '#ifdef _WIN32\n#include <Windows.h>\n#include <Wininet.h>\n#else\ntypedef unsigned long DWORD;\n#endif',
    h_text
)

h_text = re.sub(
    r'class WebPostRequest\n\{\n\tHINTERNET hInternet;\n\tHINTERNET hConnect;\n\tHINTERNET hRequest;\npublic:\n',
    'class WebPostRequest\n{\n#ifdef _WIN32\n\tHINTERNET hInternet;\n\tHINTERNET hConnect;\n\tHINTERNET hRequest;\n#endif\npublic:\n',
    h_text
)

h_text = re.sub(
    r'std::string WebPostRequest::SendPostRequest\(const std::string& jsonData\);',
    'std::string SendPostRequest(const std::string& jsonData);',
    h_text
)

with open(h_path, "w", encoding="cp1251") as f:
    f.write(h_text)

cpp_path = "/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Shared/WebRequests.cpp"
with open(cpp_path, "r", encoding="cp1251") as f:
    cpp_text = f.read()

cpp_text = re.sub(
    r'(WebPostRequest::WebPostRequest\(const wchar_t\* serverUrl, const wchar_t\* objectName, int serverPort, DWORD flags\))',
    r'#ifdef _WIN32\n\1',
    cpp_text
)

cpp_text = re.sub(
    r'(std::string WebPostRequest::SendPostRequest\(const std::string& jsonData\) \{[\s\S]*?\n\})',
    r'\1\n#else\n'
    r'WebPostRequest::WebPostRequest(const wchar_t*, const wchar_t*, int, DWORD) {}\n'
    r'void WebPostRequest::Init(const wchar_t*, const wchar_t*, int, DWORD) {}\n'
    r'WebPostRequest::~WebPostRequest() {}\n'
    r'std::string WebPostRequest::SendPostRequest(const std::string& jsonData) { return ""; }\n'
    r'#endif\n',
    cpp_text
)

with open(cpp_path, "w", encoding="cp1251") as f:
    f.write(cpp_text)
