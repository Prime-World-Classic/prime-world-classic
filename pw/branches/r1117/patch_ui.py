import os

def insert_ifndef(filepath, blocks):
    with open(filepath, 'r', encoding='cp1251', errors='ignore') as f:
        lines = f.readlines()
    
    for start_marker, end_marker in blocks:
        start_idx = -1
        end_idx = -1
        for i, line in enumerate(lines):
            if start_marker in line and start_idx == -1:
                start_idx = i
            if end_marker in line and start_idx != -1 and i > start_idx and end_idx == -1:
                end_idx = i
                break
        
        if start_idx != -1 and end_idx != -1:
            lines.insert(end_idx, "#endif\n")
            lines.insert(start_idx + 1, "#ifndef NV_LINUX_PLATFORM\n")
        else:
            print(f"Warning: Block {start_marker[:30]} not found in {filepath}")

    with open(filepath, 'w', encoding='cp1251') as f:
        f.writelines(lines)

cursor_blocks = [
    ("Image::~Image()", "hCursor = 0;"),
    ("static const char * FormatLastLastErrorMessage( DWORD errorCode )", "return \"Eror message not found\";"),
    ("static bool CreateCursors( SCursor *pCursor, const Image & image )", "return true;"),
    ("void Freeze( bool f )", "}"), # Wait, Freeze is a small function
    ("void Update( DWORD newTime )", "}"), # Might match wrong end
    ("void Render()", "}"), 
    ("void* LoadPrecompiledCursor(const nstl::string & fileName)", "return hCursor;"),
    ("bool Image::Load(const NDb::UICursorBase * pCursorDesc)", "pCursorObj = pCursorDesc;")
]

# A better way is to replace specific method bodies:
def patch_method(filepath, method_name, end_str):
    with open(filepath, 'r', encoding='cp1251', errors='ignore') as f:
        content = f.read()
    
    parts = content.split(method_name)
    if len(parts) > 1:
        after = parts[1]
        
        # find the opening brace
        brace_idx = after.find('{')
        if brace_idx != -1:
            # find end_str
            end_idx = after.find(end_str, brace_idx)
            if end_idx != -1:
                patched_after = after[:brace_idx+1] + "\n#ifndef NV_LINUX_PLATFORM\n" + after[brace_idx+1:end_idx] + "\n#endif\n" + after[end_idx:]
                content = parts[0] + method_name + patched_after
                with open(filepath, 'w', encoding='cp1251') as f:
                    f.write(content)
                return
    print(f"Failed to patch {method_name} in {filepath}")

patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "Image::~Image()", "hCursor = 0;")
patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "static const char * FormatLastLastErrorMessage( DWORD errorCode )", "return \"Eror message not found\";")
patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "static bool CreateCursors( SCursor *pCursor, const Image & image )", "return true;\n}")
patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "void Freeze( bool f )", "}\n\t}")
patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "void Update( DWORD newTime )", "NMainFrame::SetCursor( NULL );\n\t\t\t}\n\t\t}\n\t}")
patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "void Render()", "pt.y -= leftTop.y;\n")
patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "void* LoadPrecompiledCursor(const nstl::string & fileName)", "return hCursor;\n}")
patch_method("pw/branches/r1117/Src/UI/Cursor.cpp", "bool Image::Load(const NDb::UICursorBase * pCursorDesc)", "return true;\n  }")

patch_method("pw/branches/r1117/Src/UI/EditBox.cpp", "bool EditBox::EventPaste()", "return true;\n}")
patch_method("pw/branches/r1117/Src/UI/EditBox.cpp", "void EditBox::CopySelectionToClipboard()", "CloseClipboard();\n}")
