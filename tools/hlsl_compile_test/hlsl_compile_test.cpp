// hlsl_compile_test — проверка, чем компилировать HLSL клиента: какой компилятор
// принимает текущие исходники (в отличие от d3dx9_43, который на
// FullScreenMaterial даёт E5017 "Resource load type 4" из-за tex2Dlod в vs_3_0).
//
// D3DCompile грузится динамически (LoadLibrary+GetProcAddress): импортная либа
// не нужна, и на машине может быть любой d3dcompiler_XX (в Wine-префиксе наши
// d3dcompiler_46/42/40 — заглушки, D3DCompile в них возвращает E_NOTIMPL).
//
// Запуск: hlsl_compile_test <hlsl> <profile> <entry> [--def NAME=VALUE]...
//         [--inc <dir>]... [--dll <d3dcompiler_XX.dll>] [--dump <файл>]
// Профили/точки входа — как у генератора (Src/ShaderCompiler/ShaderCompiler.cpp):
// vs_3_0/ps_3_0, MainPass_VSMain/MainPass_PSMain, флаги D3DXSHADER_OPTIMIZATION_LEVEL3.
//
// --def повторяет D3DXMACRO генератора: Name = pinName из MaterialSpec.xml,
// Definition = число. Без --def компилируется с дефолтами самого .hlsl.
// --dump пишет DXBC в файл: нужен для побайтовой сверки SM3-пути «до/после»
// правки исходников (шаг 10 очереди, docs/PLAN_NVRHI.md §3.z п.2) — если
// правка не задела SM3-ветку, хэш блоба обязан совпасть.

#include <windows.h>
#include <d3dcommon.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <string>

typedef HRESULT (WINAPI *CompileFn)(const void *pSrcData, SIZE_T SrcDataSize,
                                    const char *pSourceName,
                                    const D3D_SHADER_MACRO *pDefines,
                                    ID3DInclude *pInclude, const char *pEntrypoint,
                                    const char *pTarget, UINT Flags1, UINT Flags2,
                                    ID3DBlob **ppCode, ID3DBlob **ppErrorMsgs);

typedef HRESULT (WINAPI *DisassembleFn)(const void *pSrcData, SIZE_T SrcDataSize,
                                        UINT Flags, const char *pComments,
                                        ID3DBlob **ppDisassembly);

// Дизассемблирование DXBC. Нужен в двух местах: шаг 10 — сверка «правка
// исходника эквивалентна» (байты могут разойтись из-за кодогенерации, а
// операции — совпадать), и шаг 14 — у 34 блобов OnlineShaders нет исходников,
// восстанавливать придётся по ассемблеру.
static int DoDisassemble(HMODULE h, const void *data, size_t size)
{
    DisassembleFn dis = (DisassembleFn)GetProcAddress(h, "D3DDisassemble");
    if (!dis) { printf("D3DDisassemble: НЕТ в модуле\n"); return 5; }
    ID3DBlob *txt = NULL;
    HRESULT hr = dis(data, size, 0, NULL, &txt);
    printf("D3DDisassemble hr=0x%08X\n", (unsigned)hr);
    if (SUCCEEDED(hr) && txt)
    {
        printf("%.*s", (int)txt->GetBufferSize(), (const char *)txt->GetBufferPointer());
        txt->Release();
        return 0;
    }
    return 1;
}

// ---------------------------------------------------------------- include
// Все include в наших .hlsl относительные и отсчитаны от каталога главного
// файла ("../GlobalMasks.h", "..\OnlineShaders\_commonDepthRead.h"), поэтому
// достаточно списка каталогов: каталог главного файла + явные --inc.
struct IncludeHandler : public ID3DInclude
{
    std::vector<std::string> dirs;

    // ID3DInclude — не IUnknown, в vtable только Open/Close
    static void Normalize(std::string &p)
    {
        for (size_t i = 0; i < p.size(); ++i)
            if (p[i] == '\\') p[i] = '/';
    }

    HRESULT STDMETHODCALLTYPE Open(D3D_INCLUDE_TYPE, LPCSTR pFileName,
                                   LPCVOID, LPCVOID *ppData, UINT *pBytes) override
    {
        for (size_t d = 0; d < dirs.size(); ++d)
        {
            std::string path = dirs[d] + "/" + pFileName;
            Normalize(path);
            HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE) continue;
            DWORD sz = GetFileSize(h, NULL);
            if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(h); continue; }
            char *buf = (char *)malloc(sz);
            DWORD got = 0;
            if (!ReadFile(h, buf, sz, &got, NULL) || got != sz)
            { free(buf); CloseHandle(h); continue; }
            CloseHandle(h);
            printf("  [include] %s -> %s (%lu)\n", pFileName, path.c_str(), sz);
            *ppData = buf;
            *pBytes = sz;
            return S_OK;
        }
        printf("  [include] %s -> НЕ НАЙДЕН\n", pFileName);
        return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);
    }

    HRESULT STDMETHODCALLTYPE Close(LPCVOID pData) override { free(const_cast<void*>(pData)); return S_OK; }
};

// ---------------------------------------------------------------- helpers
static std::string ReadFileAll(LPCSTR path)
{
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { printf("не открылся %s (err=%lu)\n", path, GetLastError()); return ""; }
    DWORD sz = GetFileSize(h, NULL), got = 0;
    std::string s(sz ? sz : 0, '\0');
    if (sz && (!ReadFile(h, &s[0], sz, &got, NULL) || got != sz)) s.clear();
    CloseHandle(h);
    return s;
}

static const char *D3DCompilerCandidates[] = {
    "d3dcompiler_47.dll", "d3dcompiler_48.dll", "d3dcompiler_50.dll",
    "d3dcompiler_46.dll", "d3dcompiler_43.dll", NULL
};

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("использование: %s <hlsl> <profile> <entry> [--def NAME=VALUE]... "
               "[--inc <dir>]... [--dll <dll>] [--flags <hex>] [--dump <файл>] [--disasm]\n"
               "             %s <hlsl> <profile> <entry> --blob <дxbc>  — дизассемблировать "
               "готовый блоб (компиляция не выполняется)\n", argv[0], argv[0]);
        return 2;
    }
    std::string srcPath = argv[1], profile = argv[2], entry = argv[3];
    std::vector<D3D_SHADER_MACRO> macros;
    std::vector<std::string> extraInc;
    const char *dll = NULL;
    UINT flags = 0x400;   // D3DCOMPILE_OPTIMIZATION_LEVEL3 == D3DXSHADER_OPTIMIZATION_LEVEL3
    const char *dumpPath = NULL;
    bool disasm = false;
    const char *blobPath = NULL;

    for (int i = 4; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--def") && i + 1 < argc)
        {
            char *kv = argv[++i];
            char *eq = strchr(kv, '=');
            if (!eq) { printf("плохой --def: %s\n", kv); return 2; }
            *eq = 0;
            D3D_SHADER_MACRO m; m.Name = kv; m.Definition = eq + 1;
            macros.push_back(m);
        }
        else if (!strcmp(argv[i], "--inc") && i + 1 < argc) extraInc.push_back(argv[++i]);
        else if (!strcmp(argv[i], "--dll") && i + 1 < argc) dll = argv[++i];
        else if (!strcmp(argv[i], "--flags") && i + 1 < argc) flags = (UINT)strtoul(argv[++i], NULL, 16);
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dumpPath = argv[++i];
        else if (!strcmp(argv[i], "--disasm")) disasm = true;
        else if (!strcmp(argv[i], "--blob") && i + 1 < argc) blobPath = argv[++i];
    }
    macros.push_back(D3D_SHADER_MACRO());   // терминатор {0,0}

    std::string src = ReadFileAll(srcPath.c_str());
    if (src.empty()) return 3;

    IncludeHandler inc;
    size_t slash = srcPath.find_last_of("/\\");
    inc.dirs.push_back(slash == std::string::npos ? std::string(".")
                                                  : srcPath.substr(0, slash));
    for (size_t i = 0; i < extraInc.size(); ++i) inc.dirs.push_back(extraInc[i]);

    const char *useDll = NULL;
    HMODULE h = NULL;
    for (int i = 0; dll && i == 0; ++i) { h = LoadLibraryA(dll); if (h) useDll = dll; }
    for (int i = 0; !h && D3DCompilerCandidates[i]; ++i)
        if ((h = LoadLibraryA(D3DCompilerCandidates[i]))) useDll = D3DCompilerCandidates[i];
    if (!h) { printf("ни один d3dcompiler не загрузился\n"); return 4; }
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(h, path, sizeof path);
    CompileFn compile = (CompileFn)GetProcAddress(h, "D3DCompile");
    printf("dll: %s\nD3DCompile: %s\nфайл: %s (%d байт), профиль=%s entry=%s flags=0x%X\n",
           path, compile ? "есть" : "НЕТ", srcPath.c_str(), (int)src.size(),
           profile.c_str(), entry.c_str(), flags);
    if (!compile) return 5;

    if (blobPath)
    {
        std::string blob = ReadFileAll(blobPath);
        if (blob.empty()) { printf("не открылся %s\n", blobPath); return 3; }
        printf("блоб: %s (%d байт)\n", blobPath, (int)blob.size());
        return DoDisassemble(h, blob.data(), blob.size());
    }

    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = compile(src.data(), src.size(), srcPath.c_str(),
                         macros.empty() ? NULL : &macros[0], &inc,
                         entry.c_str(), profile.c_str(), flags, 0, &code, &err);
    printf("D3DCompile hr=0x%08X%s\n", (unsigned)hr, SUCCEEDED(hr) ? " (OK)" : "");
    if (err)
    {
        const char *msg = (const char *)err->GetBufferPointer();
        SIZE_T n = err->GetBufferSize();
        if (msg && n) printf("--- сообщения компилятора ---\n%.*s\n", (int)n, msg);
        err->Release();
    }
    if (code)
    {
        printf("DXBC: %lu байт\n", (unsigned long)code->GetBufferSize());
        if (dumpPath)
        {
            FILE *f = fopen(dumpPath, "wb");
            if (f)
            {
                fwrite(code->GetBufferPointer(), 1, code->GetBufferSize(), f);
                fclose(f);
                printf("DXBC записан: %s\n", dumpPath);
            }
            else printf("не удалось записать %s\n", dumpPath);
        }
        if (disasm) DoDisassemble(h, code->GetBufferPointer(), code->GetBufferSize());
        code->Release();
    }
    return SUCCEEDED(hr) ? 0 : 1;
}
