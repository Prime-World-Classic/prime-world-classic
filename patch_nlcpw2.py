path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/NewLobbyClientPW.cpp'
with open(path, 'rb') as f:
    content = f.read()

# Replace any sequence starting with L"\xca\xee\xec\xe0\xed\xe4\xe0 and ending with :"
import re
content = re.sub(rb'L"\xca\xee\xec\xe0\xed\xe4\xe0 .*?:"', rb'L"Team:"', content)

with open(path, 'wb') as f:
    f.write(content)
