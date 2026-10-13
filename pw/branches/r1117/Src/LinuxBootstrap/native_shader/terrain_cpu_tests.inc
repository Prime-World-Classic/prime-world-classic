/** Pure admission regressions for the eight shipped ordinary terrain permutations. */
namespace {
void TerrainCpuTests(Checks& check, const Bytes& archive) {
	TerrainSpans spans{};
	const auto status = TerrainDirectory(archive,spans);
	check.Test(bool(status),"original terrain directory admits eight ordinary permutations");
	if (!status) return;
	for (size_t slot=0; slot<8; ++slot) for (size_t stage=0; stage<2; ++stage) {
		const auto span=spans[slot][stage];
		if (span.offset>archive.size() || span.size>archive.size()-span.offset) {
			check.Test(false,"terrain fixture bounds"); return;
		}
		Budget budget;
		const auto* p=MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL120,"main",archive.data()+span.offset,
			unsigned(span.size),nullptr,0,nullptr,0,Budget::Allocate,Budget::Free,&budget);
		check.Test(p && !p->error_count,"terrain bytecode CPU translation");
		if (p && !p->error_count) {
			const bool vertex=stage==0;
			check.Test(ExactTerrainReflection(p,vertex,slot),"exact shipped terrain reflection");
			check.Test(!ExactTerrainReflection(nullptr,vertex,slot),"null terrain reflection rejected");
			check.Test(!ExactTerrainReflection(p,vertex,8),"out-of-range terrain slot rejected");
			for (int kind=0; kind<12; ++kind) {
				auto changed=*p; MOJOSHADER_preshader preshader{};
				if (kind==0) changed.major_ver=2;
				if (kind==1) changed.minor_ver=1;
				if (kind==2) changed.shader_type=vertex?MOJOSHADER_TYPE_PIXEL:MOJOSHADER_TYPE_VERTEX;
				if (kind==3) changed.error_count=1;
				if (kind==4) changed.preshader=&preshader;
				if (kind==5) changed.uniforms=nullptr;
				if (kind==6) changed.attributes=nullptr;
				if (kind==7) changed.outputs=nullptr;
				if (kind==8) ++changed.uniform_count;
				if (kind==9) ++changed.attribute_count;
				if (kind==10) ++changed.output_count;
				if (kind==11) ++changed.constant_count;
				check.Test(!ExactTerrainReflection(&changed,vertex,slot),"malformed terrain reflection rejected");
			}
			for (int i=0; i<p->uniform_count; ++i) for (int kind=0; kind<4; ++kind) {
				auto values=std::vector<MOJOSHADER_uniform>(p->uniforms,p->uniforms+p->uniform_count);
				auto changed=*p; changed.uniforms=values.data();
				if (kind==0) ++values[i].index;
				if (kind==1) values[i].type=MOJOSHADER_UNIFORM_INT;
				if (kind==2) values[i].array_count=1;
				if (kind==3) values[i].constant=1;
				check.Test(!ExactTerrainReflection(&changed,vertex,slot),"terrain uniform contract exact");
			}
			for (bool output : {false,true}) {
				const auto* first=output?p->outputs:p->attributes;
				const int count=output?p->output_count:p->attribute_count;
				for (int i=0; i<count; ++i) for (int kind=0; kind<2; ++kind) {
					auto values=std::vector<MOJOSHADER_attribute>(first,first+count);
					auto changed=*p;
					if (output) changed.outputs=values.data(); else changed.attributes=values.data();
					if (kind==0) ++values[i].index; else values[i].usage=MOJOSHADER_USAGE_UNKNOWN;
					check.Test(!ExactTerrainReflection(&changed,vertex,slot),"terrain input/output semantics exact");
				}
			}
			if (vertex) {
				for (int kind=0; kind<7; ++kind) {
					auto changed=*p; auto value=p->constants[0]; changed.constants=&value;
					if (kind==0) changed.constants=nullptr;
					if (kind==1) value.type=MOJOSHADER_UNIFORM_INT;
					if (kind==2) ++value.index;
					if (kind>=3) ++value.value.f[kind-3];
					check.Test(!ExactTerrainReflection(&changed,true,slot),"terrain owns exact c0 HDR constants");
				}
				check.Test(!ExactTerrainReflection(p,true,slot^1),"local and global vertex contracts differ");
			} else {
				for (int i=0; i<p->sampler_count; ++i) for (int kind=0; kind<5; ++kind) {
					auto values=std::vector<MOJOSHADER_sampler>(p->samplers,p->samplers+p->sampler_count);
					auto changed=*p; changed.samplers=values.data();
					if (kind==0) changed.samplers=nullptr;
					if (kind==1) ++changed.sampler_count;
					if (kind==2) ++values[i].index;
					if (kind==3) values[i].type=MOJOSHADER_SAMPLER_CUBE;
					if (kind==4) values[i].texbem=1;
					check.Test(!ExactTerrainReflection(&changed,false,slot),"terrain sampler count and binding exact");
				}
				check.Test(!ExactTerrainReflection(p,false,(slot+2)%8),"different layer count rejected");
			}
		}
		MOJOSHADER_freeParseData(p);
		check.Test(budget.live==0,"terrain parser ownership balanced");
	}
	for (size_t length : {size_t(0),size_t(3),size_t(4),size_t(1923),archive.size()-1}) {
		check.Test(!TerrainDirectory(Bytes(archive.begin(),archive.begin()+length),spans),"truncated terrain archive rejected");
	}
	for (size_t pair : {size_t(0),size_t(30),size_t(120),size_t(210),size_t(239)})
		for (uint32_t value : {4u,1925u,UINT32_MAX}) {
			auto changed=archive; Put32(changed,4+pair*8,value);
			check.Test(!TerrainDirectory(changed,spans),"selected and unselected terrain offsets validated");
		}
	ScenePrograms programs;
	for (const auto& ticket : programs.terrain) check.Test(ticket.Empty(),"default terrain ticket empty");
}
}
