import re

with open("/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/WorldScript.cpp", "r") as f:
    content = f.read()

# Fix CommandExecutorWithChecker constructors
content = re.sub(
    r'(CommandExecutor0\( bool \(TObj::\*func\)\(\), TObj \*obj \) : )CommandExecutorWithChecker',
    r'\1CommandExecutorWithChecker<TObj, bool (TObj::*)(), 0>',
    content
)
content = re.sub(
    r'(CommandExecutor1\( bool \(TObj::\*func\)\( const T1 p1 \), TObj \*obj \) : )CommandExecutorWithChecker',
    r'\1CommandExecutorWithChecker<TObj, bool (TObj::*)( const T1 p1 ), 1>',
    content
)
content = re.sub(
    r'(CommandExecutor2\( bool \(TObj::\*func\)\( const T1 p1, const T2 p2 \), TObj \*obj \) : )CommandExecutorWithChecker',
    r'\1CommandExecutorWithChecker<TObj, bool (TObj::*)( const T1 p1, const T2 p2 ), 2>',
    content
)
content = re.sub(
    r'(CommandExecutor3\( bool \(TObj::\*func\)\( const T1 p1, const T2 p2, const T3 p3 \), TObj \*obj \) : )CommandExecutorWithChecker',
    r'\1CommandExecutorWithChecker<TObj, bool (TObj::*)( const T1 p1, const T2 p2, const T3 p3 ), 3>',
    content
)
content = re.sub(
    r'(CommandExecutor4\( bool \(TObj::\*func\)\( const T1 p1, const T2 p2, const T3 p3, const T4 p4 \), TObj \*obj \) : )CommandExecutorWithChecker',
    r'\1CommandExecutorWithChecker<TObj, bool (TObj::*)( const T1 p1, const T2 p2, const T3 p3, const T4 p4 ), 4>',
    content
)
content = re.sub(
    r'(CommandExecutor5\( bool \(TObj::\*func\)\( const T1 p1, const T2 p2, const T3 p3, const T4 p4, const T5 p5 \), TObj \*obj \) : )CommandExecutorWithChecker',
    r'\1CommandExecutorWithChecker<TObj, bool (TObj::*)( const T1 p1, const T2 p2, const T3 p3, const T4 p4, const T5 p5 ), 5>',
    content
)

# Fix FromString
content = re.sub(r'(!?)\bFromString\(', r'\1this->FromString(', content)

# Fix obj->*func to this->obj->*(this->func)
content = re.sub(r'\(obj->\*func\)', r'(this->obj->*(this->func))', content)

# Fix IsDebuggerPresent and __debugbreak
content = re.sub(
    r'(\s*if\s*\(\s*IsDebuggerPresent\(\)\s*\)\s*__debugbreak\(\);)',
    r'\n#ifdef _WIN32\1\n#endif',
    content
)

with open("/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/WorldScript.cpp", "w") as f:
    f.write(content)

