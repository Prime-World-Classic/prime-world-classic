path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/System/ported/types.h'
with open(path, 'rb') as f:
    content = f.read()

content = content.replace(b'typedef int HRESULT;', b'#ifndef _NV_HRESULT_DEFINED_\ntypedef int HRESULT;\n#endif')
content = content.replace(b'struct GUID {', b'#ifndef GUID_DEFINED\nstruct GUID {')
content = content.replace(b'	BYTE	Data4[ 8 ];\n	};', b'	BYTE	Data4[ 8 ];\n	};\n#endif')

with open(path, 'wb') as f:
    f.write(content)
