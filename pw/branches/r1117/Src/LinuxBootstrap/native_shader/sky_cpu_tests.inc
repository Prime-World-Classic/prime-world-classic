/** Strict shipped sky directory/reflection admission without a GL context. */
namespace {
void SkyCpuTests(Checks& check, const Bytes& archive) {
	std::array<Span,2> spans{};
	const auto status=SkyDirectory(archive,spans);
	check.Test(bool(status),"original two-pair sky directory"); if (!status) return;
	for (size_t stage=0; stage<2; ++stage) {
		const bool vertex=stage==0; const auto span=spans[stage]; Budget budget;
		const auto* p=MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL120,"main",archive.data()+span.offset,unsigned(span.size),
			nullptr,0,nullptr,0,Budget::Allocate,Budget::Free,&budget);
		check.Test(p && !p->error_count,"original sky CPU translation");
		if (p && !p->error_count) {
			check.Test(ExactSkyReflection(p,vertex),"exact original sky reflection");
			check.Test(!ExactSkyReflection(nullptr,vertex),"missing sky reflection rejected");
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
				check.Test(!ExactSkyReflection(&changed,vertex),"malformed sky reflection rejected");
			}
			for (int i=0; i<p->uniform_count; ++i) for (int kind=0; kind<4; ++kind) {
				auto values=std::vector<MOJOSHADER_uniform>(p->uniforms,p->uniforms+p->uniform_count);
				auto changed=*p; changed.uniforms=values.data();
				if (kind==0) ++values[i].index;
				if (kind==1) values[i].type=MOJOSHADER_UNIFORM_INT;
				if (kind==2) values[i].array_count=1;
				if (kind==3) values[i].constant=1;
				check.Test(!ExactSkyReflection(&changed,vertex),"sky uniform contract exact");
			}
			for (bool output : {false,true}) {
				const auto* first=output?p->outputs:p->attributes;
				const int count=output?p->output_count:p->attribute_count;
				for (int i=0; i<count; ++i) for (int kind=0; kind<2; ++kind) {
					auto values=std::vector<MOJOSHADER_attribute>(first,first+count);
					auto changed=*p; if (output) changed.outputs=values.data(); else changed.attributes=values.data();
					if (kind==0) ++values[i].index; else values[i].usage=MOJOSHADER_USAGE_UNKNOWN;
					check.Test(!ExactSkyReflection(&changed,vertex),"sky input/output semantics exact");
				}
			}
			if (vertex) for (int kind=0; kind<7; ++kind) {
				auto changed=*p; auto c=p->constants[0]; changed.constants=&c;
				if (kind==0) changed.constants=nullptr;
				if (kind==1) ++c.index;
				if (kind==2) c.type=MOJOSHADER_UNIFORM_INT;
				if (kind>=3) ++c.value.f[kind-3];
				check.Test(!ExactSkyReflection(&changed,true),"sky owns exact affine c3 constant");
			} else for (int kind=0; kind<5; ++kind) {
				auto changed=*p; auto sampler=p->samplers[0]; changed.samplers=&sampler;
				if (kind==0) changed.samplers=nullptr;
				if (kind==1) ++changed.sampler_count;
				if (kind==2) ++sampler.index;
				if (kind==3) sampler.type=MOJOSHADER_SAMPLER_CUBE;
				if (kind==4) sampler.texbem=1;
				check.Test(!ExactSkyReflection(&changed,false),"sky sampler exact");
			}
		}
		MOJOSHADER_freeParseData(p); check.Test(budget.live==0,"sky parser ownership balanced");
	}
	for (size_t length : {size_t(0),size_t(3),size_t(4),size_t(19),archive.size()-1})
		check.Test(!SkyDirectory(Bytes(archive.begin(),archive.begin()+length),spans),"truncated sky archive rejected");
	for (size_t offset : {size_t(4),size_t(8),size_t(12),size_t(16)}) for (uint32_t value : {4u,21u,UINT32_MAX}) {
		auto changed=archive; Put32(changed,offset,value);
		check.Test(!SkyDirectory(changed,spans),"selected and unselected sky directory offsets validated");
	}
}
}
