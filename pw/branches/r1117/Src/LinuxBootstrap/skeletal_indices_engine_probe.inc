/** Verify exporter BGRA bone storage independently of the native skinning implementations. */
bool RunLinuxSkeletalIndicesEngineProbe()
{
	unsigned checks=0, failures=0;
	const auto check=[&](bool good, const char* label) {
		++checks; if (!good) ++failures;
		fprintf(stdout,"Skeletal indices probe: %s result=%s\n",label,good ? "PASS" : "FAIL");
	};
	struct Vertex { CVec3 position, normal; float weights[4]; unsigned char indices[4]; };
	const unsigned char logical[4]={3,1,0,2};
	const Render::Color packed(logical[0],logical[1],logical[2],logical[3]);
	check(sizeof(packed)==4 && packed.R==3 && packed.B==0 &&
		reinterpret_cast<const unsigned char*>(&packed)[0]==0,"exporter Color stores B,G,R,A bytes");
	Matrix43 bones[4];
	for (unsigned i=0; i<4; ++i) {
		Identity(&bones[i]);
		const float angle=float(i)*.3f;
		bones[i]._11=cosf(angle); bones[i]._12=-sinf(angle);
		bones[i]._21=sinf(angle); bones[i]._22=cosf(angle);
		bones[i]._14=float(i)-1.5f; bones[i]._34=float(i)*.15f;
	}
	const unsigned short indices[6]={0,1,2,0,2,3};
	std::unique_ptr<IDirect3DIndexBuffer9> ib(Render::CreateIB16(sizeof(indices),Render::RENDER_POOL_MANAGED,indices).GetPtr());
	const CVec3 positions[4]={{-.4f,0,-.4f},{.4f,0,-.4f},{.4f,0,.4f},{-.4f,0,.4f}};
	const float weights[][4]={{1,0,0,0},{0,1,0,0},{0,0,1,0},{0,0,0,1},{.6f,.2f,.15f,.05f}};
	glPushAttrib(GL_ALL_ATTRIB_BITS); glPushClientAttrib(GL_CLIENT_PIXEL_STORE_BIT);
	GLint matrixMode=0; glGetIntegerv(GL_MATRIX_MODE,&matrixMode);
	glViewport(0,0,128,128); glPixelStorei(GL_PACK_ALIGNMENT,1);
	glDisable(GL_SCISSOR_TEST); glDisable(GL_LIGHTING); glDisable(GL_BLEND); glDisable(GL_TEXTURE_2D);
	glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE); glDisable(GL_DEPTH_TEST);
	glColorMask(1,1,1,1); glDepthMask(GL_TRUE); glClearColor(0,0,0,1);
	glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity(); glOrtho(-3,3,-3,3,-20,20);
	glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
	using Pixels=std::array<unsigned char,128*128*4>;
	const auto draw=[&](const Vertex* vertices, Render::EVertexElementType indexType, bool skinned) {
		Render::VertexFormatDescriptor format;
		format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,position),Render::VERTEXELEMENTTYPE_FLOAT3,Render::VERETEXELEMENTUSAGE_POSITION,0));
		format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,normal),Render::VERTEXELEMENTTYPE_FLOAT3,Render::VERETEXELEMENTUSAGE_NORMAL,0));
		if (skinned) {
			format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,weights),Render::VERTEXELEMENTTYPE_FLOAT4,Render::VERETEXELEMENTUSAGE_BLENDWEIGHT,0));
			format.AddVertexElement(Render::VertexElementDescriptor(0,offsetof(Vertex,indices),indexType,Render::VERETEXELEMENTUSAGE_BLENDINDICES,0));
		}
		std::unique_ptr<IDirect3DVertexBuffer9> vb(Render::CreateVB(sizeof(Vertex)*4,Render::RENDER_POOL_MANAGED,vertices).GetPtr());
		Render::Primitive primitive;
		primitive.SetVertexDeclaration(Render::SmartRenderer::GetVertexFormatDeclaration(format));
		primitive.SetVertexStream(vb.get(),sizeof(Vertex)); primitive.SetIndexBuffer(ib.get());
		primitive.GetDipDescriptor()=Render::DipDescriptor(Render::RENDERPRIMITIVE_TRIANGLELIST,0,0,4,0,2);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
		Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0,0,0,1);
		Render::SmartRenderer::SetOpenGLImmediateSkeletalMatrices(skinned ? bones : nullptr,skinned ? 4 : 0);
		primitive.Bind(); primitive.Draw();
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		Render::SmartRenderer::UnBindVertexBuffer(0); Render::SmartRenderer::BindIndexBuffer(DXIndexBufferRef());
		Pixels pixels{}; glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
		return pixels;
	};
	for (auto type : {Render::VERTEXELEMENTTYPE_D3DCOLOR,Render::VERTEXELEMENTTYPE_UBYTE4})
		for (const auto& weight : weights) {
			Vertex source[4]{}, reference[4]{};
			for (unsigned v=0; v<4; ++v) {
				source[v].position=positions[v]; source[v].normal=CVec3(0,-1,0);
				memcpy(source[v].weights,weight,sizeof(weight));
				memcpy(source[v].indices,type==Render::VERTEXELEMENTTYPE_D3DCOLOR ? static_cast<const void*>(&packed) : logical,4);
				reference[v].position=CVec3(0,0,0); reference[v].normal=CVec3(0,0,0);
				for (unsigned i=0; i<4; ++i) {
					reference[v].position+=Transform(source[v].position,bones[logical[i]])*weight[i];
					reference[v].normal+=Rotate(source[v].normal,bones[logical[i]])*weight[i];
				}
				reference[v].normal*=1.f/sqrtf(reference[v].normal.LengthSqr());
			}
			const auto expected=draw(reference,type,false), actual=draw(source,type,true);
			unsigned covered=0;
			for (size_t i=0; i<expected.size(); i+=4) if (expected[i] || expected[i+1] || expected[i+2]) ++covered;
			check(covered>100,"independent weighted reference is visible");
			check(actual==expected,type==Render::VERTEXELEMENTTYPE_D3DCOLOR ?
				"D3DCOLOR OpenGL path pairs RGBA indices with original weights" : "UBYTE4 OpenGL path preserves unswizzled byte order");
		}
	CObj<Stream> stream=RootFileSystem::OpenFile("Heroes/Plane/_.skin",FILEACCESS_READ,FILEOPEN_OPEN_EXISTING);
	check(IsValid(stream) && stream->IsOk() && stream->GetSize()>int(sizeof(H6GeometryFileHeader)) &&
		stream->GetSize()<16*1024*1024,"original Plane skin is bounded and readable");
	if (IsValid(stream) && stream->IsOk() && stream->GetSize()>int(sizeof(H6GeometryFileHeader)) && stream->GetSize()<16*1024*1024) {
		std::vector<unsigned char> bytes(stream->GetSize());
		check(stream->Read(bytes.data(),bytes.size())==int(bytes.size()),"read complete original skin");
		const auto& header=*reinterpret_cast<const H6GeometryFileHeader*>(bytes.data());
		bool valid=StaticVectorPayloadRangeValid(header.fragments,bytes.data(),bytes.size()) && header.fragments.size()>0 &&
			header.vbOffset<bytes.size() && header.commonVertexBufferSizeInBytes<=bytes.size()-header.vbOffset &&
			header.reindexOffset<bytes.size() && bytes.size()-header.reindexOffset>=sizeof(GeometryReindex);
		check(valid,"original skin header ranges are valid");
		unsigned vertices=0, unequal=0, mismatched=0, byteMismatched=0, bad=0;
		if (valid) {
			const auto* geometry=Render::RenderResourceManager::LoadSkeletalMeshGeometry("Heroes/Plane/_.skin");
			check(geometry && geometry->primitives.size()==header.fragments.size(),"original renderer loads the same fragment count");
			const auto* reindex=reinterpret_cast<const GeometryReindex*>(bytes.data()+header.reindexOffset);
			std::vector<char> alternate(bytes.begin()+header.vbOffset,bytes.begin()+header.vbOffset+header.commonVertexBufferSizeInBytes);
			for (unsigned f=0; f<header.fragments.size(); ++f) {
				const auto& fragment=header.fragments[f];
				unsigned weightsOffset=0, indicesOffset=0;
				Render::EVertexElementType type=Render::VERTEXELEMENTTYPE_UBYTE4;
				const StaticVector<unsigned short>* remap=nullptr;
				const bool ready=FindLinuxMeshSkinningElements(fragment,bytes.data(),bytes.size(),&weightsOffset,&indicesOffset,&type) &&
					type==Render::VERTEXELEMENTTYPE_D3DCOLOR && ResolveLinuxHeroMeshFragmentReindex(reindex,f,bytes.data(),bytes.size(),&remap);
				check(ready,"original fragment declares D3DCOLOR and resolves its bone palette");
				if (!ready) continue;
				const char* vb=reinterpret_cast<const char*>(bytes.data()+header.vbOffset);
				for (unsigned v=0; v<fragment.numVertices; ++v) {
					const size_t offset=fragment.streamOffset+size_t(fragment.baseVertexIndex+v)*fragment.vertexStrideSizeInBytes+indicesOffset;
					if (offset>header.commonVertexBufferSizeInBytes || header.commonVertexBufferSizeInBytes-offset<4) { ++bad; continue; }
					Render::Color channels; memcpy(&channels,vb+offset,4);
					const unsigned char expected[4]={channels.R,channels.G,channels.B,channels.A};
					memcpy(alternate.data()+offset,expected,4);
					LinuxHeroMeshPreviewVertex decoded; bool remapped=false; size_t invalid=0;
					const bool read=ReadLinuxHeroMeshPreviewSkinning(fragment,v,weightsOffset,indicesOffset,type,remap,header.jointsCount,
						vb,header.commonVertexBufferSizeInBytes,&decoded,&remapped,&invalid);
					if (!read || !remapped || invalid) { ++bad; continue; }
					++vertices; if (channels.R!=channels.B) ++unequal;
					for (unsigned i=0; i<4; ++i)
						if (expected[i]>=remap->size() || decoded.blendIndices[i]!=(*remap)[expected[i]]) ++mismatched;
					LinuxHeroMeshPreviewVertex plain;
					if (!ReadLinuxHeroMeshPreviewSkinning(fragment,v,weightsOffset,indicesOffset,Render::VERTEXELEMENTTYPE_UBYTE4,
						remap,header.jointsCount,alternate.data(),alternate.size(),&plain,nullptr,nullptr)) ++byteMismatched;
					for (unsigned i=0; i<4; ++i)
						if (plain.blendIndices[i]!=decoded.blendIndices[i] || plain.blendWeights[i]!=decoded.blendWeights[i]) ++byteMismatched;
				}
				// Compare the actual engine primitive to independently pre-skinned original vertices.
				if (!geometry || f>=geometry->primitives.size()) continue;
				auto* primitive=geometry->primitives[f];
				unsigned positionOffset=0, normalOffset=0;
				if (!primitive || !FindLinuxMeshPositionElement(fragment,bytes.data(),bytes.size(),&positionOffset) ||
					!FindLinuxMeshNormalElement(fragment,bytes.data(),bytes.size(),&normalOffset)) { check(false,"original draw layout"); continue; }
				std::vector<Matrix43> palette(remap->size());
				for (size_t i=0; i<palette.size(); ++i) {
					Identity(&palette[i]); const float phase=(*remap)[i]*.09f;
					palette[i]._11=cosf(phase); palette[i]._12=-sinf(phase);
					palette[i]._21=sinf(phase); palette[i]._22=cosf(phase);
					palette[i]._14=sinf(phase)*.3f; palette[i]._34=cosf(phase)*.2f;
				}
				std::vector<char> transformed(vb,vb+header.commonVertexBufferSizeInBytes);
				bool referenceValid=true;
				CVec3 minimum(0,0,0), maximum(0,0,0);
				for (unsigned v=0; v<fragment.numVertices; ++v) {
					const size_t base=fragment.streamOffset+size_t(fragment.baseVertexIndex+v)*fragment.vertexStrideSizeInBytes;
					if (base>transformed.size() || fragment.vertexStrideSizeInBytes>transformed.size()-base ||
						positionOffset+12>fragment.vertexStrideSizeInBytes || normalOffset+12>fragment.vertexStrideSizeInBytes ||
						weightsOffset+16>fragment.vertexStrideSizeInBytes || indicesOffset+4>fragment.vertexStrideSizeInBytes) { referenceValid=false; break; }
					CVec3 pos, normal; float w[4]; Render::Color color;
					memcpy(&pos,vb+base+positionOffset,12); memcpy(&normal,vb+base+normalOffset,12);
					memcpy(w,vb+base+weightsOffset,16); memcpy(&color,vb+base+indicesOffset,4);
					const unsigned char index[4]={color.R,color.G,color.B,color.A};
					CVec3 result(0,0,0), resultNormal(0,0,0); float total=0;
					for (unsigned i=0; i<4; ++i) if (w[i]>.0001f) {
						if (index[i]>=palette.size()) { referenceValid=false; break; }
						result+=Transform(pos,palette[index[i]])*w[i]; resultNormal+=Rotate(normal,palette[index[i]])*w[i]; total+=w[i];
					}
					if (!referenceValid || total<=.0001f) { referenceValid=false; break; }
					if (fabsf(total-1.f)>.02f) { result/=total; resultNormal/=total; }
					if (resultNormal.LengthSqr()>.000001f) resultNormal*=1.f/sqrtf(resultNormal.LengthSqr());
					else resultNormal=normal;
					memcpy(transformed.data()+base+positionOffset,&result,12);
					memcpy(transformed.data()+base+normalOffset,&resultNormal,12);
					if (!v) minimum=maximum=result;
					else {
						minimum.x=std::min(minimum.x,result.x); maximum.x=std::max(maximum.x,result.x);
						minimum.y=std::min(minimum.y,result.y); maximum.y=std::max(maximum.y,result.y);
						minimum.z=std::min(minimum.z,result.z); maximum.z=std::max(maximum.z,result.z);
					}
				}
				const float span=std::max({maximum.x-minimum.x,maximum.y-minimum.y,maximum.z-minimum.z});
				referenceValid=referenceValid && std::isfinite(span) && span>0;
				check(referenceValid,"independent real-mesh deformation stays within validated palette and vertex ranges");
				if (!referenceValid) continue;
				std::unique_ptr<IDirect3DVertexBuffer9> referenceVB(Render::CreateVB(transformed.size(),Render::RENDER_POOL_MANAGED,transformed.data()).GetPtr());
				Render::Primitive reference; reference.SetVertexDeclaration(primitive->GetVertexDeclaration());
				reference.SetIndexBuffer(primitive->GetIndexBuffer()); reference.SetVertexStream(referenceVB.get(),fragment.vertexStrideSizeInBytes,fragment.streamOffset);
				reference.GetDipDescriptor()=primitive->GetDipDescriptor();
				const auto realDraw=[&](bool deform) {
					glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
					glMatrixMode(GL_MODELVIEW); glLoadIdentity(); glRotatef(25,1,0,0); glRotatef(35,0,1,0);
					Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
					Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform((minimum.x+maximum.x)*.5f,
						(minimum.y+maximum.y)*.5f,(minimum.z+maximum.z)*.5f,4.f/span);
					Render::SmartRenderer::SetOpenGLImmediateSkeletalMatrices(deform ? palette.data() : nullptr,deform ? palette.size() : 0);
					auto& mesh=deform ? *primitive : reference; mesh.Bind(); mesh.Draw();
					Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
					Render::SmartRenderer::UnBindVertexBuffer(0); Render::SmartRenderer::BindIndexBuffer(DXIndexBufferRef());
					Pixels pixels{}; glReadPixels(0,0,128,128,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()); return pixels;
				};
				const auto expected=realDraw(false), actual=realDraw(true);
				unsigned covered=0;
				for (size_t i=0; i<expected.size(); i+=4) if (expected[i] || expected[i+1] || expected[i+2]) ++covered;
				check(covered>100,"original mesh reference has nonblank geometry");
				check(actual==expected,"original engine primitive matches independent BGRA-decoded reference pixels");
				fprintf(stdout,"Skeletal original fragment=%u vertices=%u pixels=%u equal=%u\n",f,fragment.numVertices,covered,unsigned(actual==expected));
			}
		}
		check(vertices>1000 && unequal>100 && bad==0,"real vertices exercise asymmetric bone channels and reindexing");
		check(mismatched==0,"every original preview vertex uses the exporter weight-to-bone association");
		check(byteMismatched==0,"plain-byte preview decoding retains weight order and the same original bone remap");
		fprintf(stdout,"Skeletal original vertices=%u asymmetric=%u mismatched=%u invalid=%u\n",vertices,unequal,mismatched,bad);
	}
	Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(0,0,0,1);
	glMatrixMode(GL_MODELVIEW); glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix();
	glMatrixMode(matrixMode); glPopClientAttrib(); glPopAttrib();
	check(glGetError()==GL_NO_ERROR,"skinning checks leave no GL error");
	fprintf(stdout,"Skeletal indices summary: checks=%u failures=%u\n",checks,failures);
	return failures==0;
}
