path = '/home/vitaly/p/Prime-World/pw/branches/r1117/Src/Render/configdatabase.cpp'
with open(path, 'rb') as f:
    content = f.read()

# Replace any case of StdAfx.h or stdafx.h
import re
content = re.sub(rb'#include "[Ss]td[Aa]fx\.h"', b'#include "stdafx.h"\n#ifdef _WIN32', content)

if b'#else // _WIN32' not in content:
    stubs = b"""
#else // _WIN32
#include "configdatabase.h"

class ConfigDatabaseStub : public IConfigDatabase {
public:
    virtual bool Load(LPCTSTR FileName, const SOUND_DEVICE& soundDevice, const D3DADAPTER_IDENTIFIER9& DDid, const D3DCAPS9&, DWORD SystemMemory, DWORD VideoMemory, DWORD CPUSpeed) { return true; }
    virtual void Release() { delete this; }
    virtual const vector<StringPair>& GetAggregateProperties() const { static vector<StringPair> v; return v; }
    virtual const vector<StringPair>* GetNamedProperties(const char*) const { return NULL; }
    virtual unsigned int GetDevicePropertyCount() const { return 0; }
    virtual const char* GetDeviceProperty(unsigned int) const { return ""; }
    virtual const char* GetDeviceValue(unsigned int) const { return ""; }
    virtual unsigned int GetRequirementsPropertyCount() const { return 0; }
    virtual const char* GetRequirementsProperty(unsigned int) const { return ""; }
    virtual const char* GetRequirementsValue(unsigned int) const { return ""; }
    virtual const char* GetGfxDeviceString() const { return ""; }
    virtual const char* GetGfxVendorString() const { return ""; }
    virtual const char* GetSoundDeviceString() const { return ""; }
    virtual const char* GetSoundVendorString() const { return ""; }
    virtual bool IsError() const { return false; }
    virtual const char* GetErrorString() const { return ""; }
};

IConfigDatabase* IConfigDatabase::Create() { return new ConfigDatabaseStub(); }
#endif // _WIN32
"""
    content += stubs

with open(path, 'wb') as f:
    f.write(content)
