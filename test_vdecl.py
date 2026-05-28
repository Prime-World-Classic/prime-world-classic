import sys
content = open("pw/branches/r1117/Src/Render/GLRenderer.cpp").read()

content = content.replace("STDMETHODIMP GLDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* p) { m_pVertexDecl=p; return D3D_OK; }",
                          "STDMETHODIMP GLDirect3DDevice9::SetVertexDeclaration(IDirect3DVertexDeclaration9* p) { m_pVertexDecl=p; if (p) { auto& els = ((GLDirect3DVertexDeclaration9*)p)->GetElements(); for (auto& e : els) { printf(\"VDecl Stream=%d, Offset=%d, Type=%d, Method=%d, Usage=%d, UsageIndex=%d\\n\", e.Stream, e.Offset, e.Type, e.Method, e.Usage, e.UsageIndex); } } fflush(stdout); return D3D_OK; }")

open("pw/branches/r1117/Src/Render/GLRenderer.cpp", "w").write(content)
