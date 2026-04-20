path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/PW_Client/NewLobbyClientPW.cpp'
with open(path, 'rb') as f:
    content = f.read()

# Replace Russian text with English to avoid encoding issues on Linux
content = content.replace(b'L"\xca\xee\xec\xe0\xed\xe4\xe0 \xef\xee\xec\xe5\xf9\xe8\xea\xee\xe2:"', b'L"Team 2:"')
content = content.replace(b'L"\xca\xee\xec\xe0\xed\xe4\xe0 \xe0\xe4\xee\xf0\xed\xe8\xec\xee\xe2:"', b'L"Team 1:"')

# If the above fails due to slightly different bytes, let's try more broad match
# The garbage shown in grep was  :
# \xca\xee\xec\xe0\xed\xe4\xe0 is "Команда"

with open(path, 'wb') as f:
    f.write(content)
