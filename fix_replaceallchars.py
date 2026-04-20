import re

with open("/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/WorldScript.cpp", "rb") as f:
    content = f.read()

# The weird character in the source
content = content.replace(b"line[i] = '\xff';", b"line[i] = '\\xff';")
content = content.replace(b"NStr::ReplaceAllChars( &args[i], '\xff', ' ' );", b"NStr::ReplaceAllChars( &args[i], '\\xff', ' ' );")
# In case it's actually UTF-8 in the file now
content = content.replace("line[i] = '';".encode('utf-8'), b"line[i] = '\\xff';")
content = content.replace("NStr::ReplaceAllChars( &args[i], '', ' ' );".encode('utf-8'), b"NStr::ReplaceAllChars( &args[i], '\\xff', ' ' );")

with open("/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PF_GameLogic/WorldScript.cpp", "wb") as f:
    f.write(content)
