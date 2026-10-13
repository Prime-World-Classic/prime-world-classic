/** White-box CPU tests of the one selected baked program and packed colour contract. */
namespace {
void BakedCpuTests(Checks& check, const Bytes& archive) {
	std::array<Span,2> spans{};
	check.Test(bool(BakedDirectory(archive,spans)), "actual BasicMaterial directory admits baked pair24");
	// Parse measured fixture spans only after verifying all bounds, even for negative baselines.
	if (archive.size() != 1960136 || Read32(archive,0) != 36864) {
		check.Test(false,"baked fixture identity before reflection mutation"); return;
	}
	const std::array<Span,2> expected{{{308400,592},{304104,348}}};
	for (size_t stage=0; stage<2; ++stage) {
		check.Test(spans[stage].offset==expected[stage].offset && spans[stage].size==expected[stage].size,
			"exact baked stage offset and length");
		Budget budget;
		const auto s=expected[stage];
		const auto* parsed=MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSL120,"main",archive.data()+s.offset,
			unsigned(s.size),nullptr,0,nullptr,0,Budget::Allocate,Budget::Free,&budget);
		check.Test(parsed && !parsed->error_count,"original baked stage CPU translation");
		if (parsed && !parsed->error_count) {
			const bool vertex=stage==0;
			check.Test(ExactBakedReflection(parsed,vertex),"exact original baked reflection");
			check.Test(!ExactBakedReflection(nullptr,vertex),"null baked reflection rejected");
			for (int kind=0; kind<8; ++kind) {
				auto changed=*parsed; MOJOSHADER_preshader preshader{};
				if (kind==0) changed.major_ver=2;
				if (kind==1) changed.minor_ver=1;
				if (kind==2) changed.shader_type=vertex?MOJOSHADER_TYPE_PIXEL:MOJOSHADER_TYPE_VERTEX;
				if (kind==3) changed.error_count=1;
				if (kind==4) changed.preshader=&preshader;
				if (kind==5) changed.uniforms=nullptr;
				if (kind==6) changed.attributes=nullptr;
				if (kind==7) changed.outputs=nullptr;
				check.Test(!ExactBakedReflection(&changed,vertex),"malformed baked reflection envelope rejected");
			}
			for (int kind=0; kind<7; ++kind) {
				auto uniforms=std::vector<MOJOSHADER_uniform>(parsed->uniforms,parsed->uniforms+parsed->uniform_count);
				auto changed=*parsed; changed.uniforms=uniforms.data();
				if(kind==0) --changed.uniform_count;
				if(kind==1) ++changed.uniform_count;
				if(kind==2) uniforms[0].type=MOJOSHADER_UNIFORM_INT;
				if(kind==3) uniforms[0].array_count=1;
				if(kind==4) uniforms[0].constant=1;
				if(kind==5) uniforms[0].index=uniforms[1].index;
				if(kind==6) uniforms.back().index=vertex?233:22;
				check.Test(!ExactBakedReflection(&changed,vertex),"wrong baked uniform shape/register rejected");
			}
			for (bool outputs : {false,true}) for (int kind=0; kind<5; ++kind) {
				const auto* source=outputs?parsed->outputs:parsed->attributes;
				const auto count=outputs?parsed->output_count:parsed->attribute_count;
				auto attrs=std::vector<MOJOSHADER_attribute>(source,source+count);
				auto changed=*parsed;
				if(outputs) changed.outputs=attrs.data(); else changed.attributes=attrs.data();
				if(kind<2) { if(outputs) changed.output_count+=kind?1:-1; else changed.attribute_count+=kind?1:-1; }
				if(kind==2) attrs[0].usage=MOJOSHADER_USAGE_DEPTH;
				if(kind==3) ++attrs.back().index;
				if(kind==4) attrs.back().usage=MOJOSHADER_USAGE_NORMAL;
				check.Test(!ExactBakedReflection(&changed,vertex),"unexpected baked input/output rejected");
			}
			if(vertex) {
				for(int kind=0;kind<8;++kind) {
					auto changed=*parsed; auto constant=parsed->constants[0]; changed.constants=&constant;
					if(kind==0) changed.constant_count=0;
					if(kind==1) changed.constant_count=2;
					if(kind==2) changed.constants=nullptr;
					if(kind==3) constant.index=4;
					if(kind==4) constant.value.f[2]=1;
					if(kind==5) constant.type=MOJOSHADER_UNIFORM_INT;
					if(kind==6) constant.value.f[0]=std::numeric_limits<float>::quiet_NaN();
					if(kind==7) changed.sampler_count=1;
					check.Test(!ExactBakedReflection(&changed,true),"wrong baked shader-owned constants or VS sampler rejected");
				}
			} else for(int kind=0;kind<7;++kind) {
				auto changed=*parsed; auto sampler=parsed->samplers[0]; changed.samplers=&sampler;
				if(kind==0) changed.sampler_count=0;
				if(kind==1) changed.sampler_count=2;
				if(kind==2) changed.samplers=nullptr;
				if(kind==3) sampler.index=1;
				if(kind==4) sampler.type=MOJOSHADER_SAMPLER_CUBE;
				if(kind==5) sampler.texbem=1;
				if(kind==6) changed.constant_count=1;
				check.Test(!ExactBakedReflection(&changed,false),"wrong baked PS sampler/constant rejected");
			}
		}
		MOJOSHADER_freeParseData(parsed);
		check.Test(budget.live==0,"baked CPU parse allocations released");
	}
	for(size_t length : {size_t(0),size_t(3),size_t(4),size_t(294915)}) {
		const Bytes shortArchive(archive.begin(),archive.begin()+length);
		check.Test(!BakedDirectory(shortArchive,spans),"truncated baked directory rejected");
	}
	for(size_t pair : {size_t(0),size_t(24),size_t(36863)}) for(uint32_t off : {4u,294917u,UINT32_MAX,304104u}) {
		auto changed=archive; Put32(changed,4+pair*8,off);
		check.Test(!BakedDirectory(changed,spans),"bad selected/unselected baked stage directory rejected");
	}
	for(uint32_t count : {96u,0u,UINT32_MAX}) {
		auto changed=archive; Put32(changed,0,count);
		check.Test(!BakedDirectory(changed,spans),"wrong baked archive count rejected");
	}
	for(size_t off : {size_t(308400),size_t(308400+592-4),size_t(304104+348-4)}) {
		auto changed=archive; Put32(changed,off,0);
		check.Test(!BakedDirectory(changed,spans),"wrong baked stage version/end rejected");
	}
	Bytes day(43,201),night(59,202),output;
	for(size_t index=0;index<5;++index) for(size_t channel=0;channel<4;++channel) {
		day[3+index*8+channel]=static_cast<unsigned char>(10*index+channel);
		night[7+index*10+channel]=static_cast<unsigned char>(100+10*index+channel);
	}
	const Bgra8ColorStream d{{day.data(),day.size()},3,8},n{{night.data(),night.size()},7,10};
	const std::vector<uint32_t> indices{4,2,1,4,1,2};
	check.Test(bool(StageBakedColors(d,n,indices,128,output)),"padded independent BGRA streams admitted");
	check.Test(output.size()==40,"baked staging includes base-vertex address range");
	if(output.size()==40) for(size_t index : {size_t(1),size_t(2),size_t(4)}) {
		const std::array<unsigned char,8> expectedColor{static_cast<unsigned char>(10*index+2),static_cast<unsigned char>(10*index+1),static_cast<unsigned char>(10*index),static_cast<unsigned char>(10*index+3),static_cast<unsigned char>(100+10*index+2),static_cast<unsigned char>(100+10*index+1),static_cast<unsigned char>(100+10*index),static_cast<unsigned char>(100+10*index+3)};
		check.Test(std::equal(expectedColor.begin(),expectedColor.end(),output.begin()+index*8),"BGRA conversion preserves alpha bytes without HDR premultiplication");
	}
	for(bool changeDay : {false,true}) for(int kind=0;kind<9;++kind) {
		auto changed=changeDay?d:n;
		if(kind==0) changed.bytes.data=nullptr;
		if(kind==1) changed.bytes.size=0;
		if(kind==2) changed.offset=SIZE_MAX;
		if(kind==3) changed.offset=changed.bytes.size;
		if(kind==4) changed.stride=0;
		if(kind==5) changed.stride=3;
		if(kind==6) changed.stride=SIZE_MAX;
		if(kind==7) changed.bytes.size=changed.offset+4*changed.stride+3;
		if(kind==8) changed.bytes.size=129;
		output={1,2,3};
		check.Test(!StageBakedColors(changeDay?changed:d,changeDay?n:changed,indices,128,output) && output.empty(),
			"malformed day/night bounds clear unpublished staging");
	}
	for(const auto& refs : {std::vector<uint32_t>{},std::vector<uint32_t>{5},std::vector<uint32_t>{UINT32_MAX}})
		check.Test(!StageBakedColors(d,n,refs,128,output) && output.empty(),"missing/overflow colour indices rejected");
	const Bgra8ColorStream dense{{day.data(),20},0,4};
	check.Test(!StageBakedColors(dense,dense,{4},39,output) && output.empty(),"converted colour allocation independently capped");
	check.Test(bool(StageBakedColors(dense,dense,{4},40,output)) && output.size()==40,"exact converted colour allocation cap accepted");
	StaticPrograms refs;
	check.Test(refs.basicFx.simple.Empty() && refs.basicFx.additiveOpacity.Empty() && refs.basicFx.blendOpacity.Empty() && refs.bakedBasicMaterial.Empty(),
		"combined family defaults to four empty CPU tickets");
}
}
