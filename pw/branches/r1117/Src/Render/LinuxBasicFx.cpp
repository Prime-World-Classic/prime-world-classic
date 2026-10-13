#include "stdafx.h"
#include "LinuxBasicFx.h"
#include "LinuxPreviewSampler.h"

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include "DBRender.h"
#include "MaterialSpec.h"
#include "VertexColorStream.h"
#include "texture.h"
#include "../System/FileSystem/FileSystem.h"
#include "../System/Stream.h"
#include "../System/matrix43.h"
#include <GL/gl.h>
#include <GL/glx.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <new>
#include <vector>

namespace Render { namespace LinuxBasicFx {
namespace {
uint64_t lifetime = 0;
std::unique_ptr<pwshader::ShaderBackend> backend;
pwshader::ScenePrograms programs;
Statistics statistics;
bool selected = false, attempted = false;
bool selectedBaked = false, selectedBakedValid = false;
bool selectedSky = false, selectedSkyValid = false;
unsigned selectedMipLevels = 1;
bool transparentDrawing = false;
NDb::BlendMode selectedBlend = NDb::BLENDMODE_OFF;
bool selectedRasterValid = false;
bool selectedOpacityValid = false;
NDb::OpacityAndEmissiveModePin selectedOpacity = NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE;
pwshader::AlphaTest alphaTest = pwshader::AlphaTest::Disabled;
pwshader::CullMode cullMode = pwshader::CullMode::Back;
int alphaReference = 127;
unsigned int selectedTexture = 0;
pwshader::Float4 uv{}, multiply{}, add{}, opacityAndOthers{};
pwshader::Float4 night{};
pwshader::Bgra8ColorStream dayColors, nightColors;

pwshader::ContextKey Current(void*) noexcept {
	return {reinterpret_cast<uintptr_t>(glXGetCurrentContext()), lifetime};
}
void* Lookup(const char* name, void*) {
	return reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
}
bool Reject(const char* message) {
	if (selectedBaked) ++statistics.bakedRejected;
	if (selectedSky) ++statistics.skyRejected;
	if (++statistics.rejected == 1)
		fprintf(stderr, "Native %s draw rejected: %s\n", selectedSky ? "SkyMaterial" : selectedBaked ? "baked BasicMaterial" : "BasicFX", message);
	return false;
}
/** Shared classification prevents the collector and material preparation from disagreeing. */
const NDb::BasicFXMaterial* AdmittedMaterial(const NDb::Material* material) {
	if (!material || material->GetObjectTypeID() != NDb::BasicFXMaterial::typeId) return nullptr;
	const auto& m = *static_cast<const NDb::BasicFXMaterial*>(material);
	const auto& sampler = m.DiffuseMap.samplerState;
	if (m.SkeletalMeshPinValue != NDb::BOOLEANPIN_NONE ||
		m.MultiplyVertexColorPinValue != NDb::BOOLEANPIN_NONE ||
		m.OpacityAndEmissiveModePinValue == NDb::OPACITYANDEMISSIVEMODEPIN_EMISSIVE ||
		m.DistortionPinValue != NDb::DISTORTIONPIN_DISTORTNONE || m.ignoreRenderState || m.isBlendModeOverrided ||
		m.opacity.IsAnimated() ||
		m.DiffuseMul.IsAnimated() || m.DiffuseAdd.IsAnimated() || m.uOffset.IsAnimated() || m.vOffset.IsAnimated() ||
		sampler.addressU != NDb::TEXTUREADDRESSTYPE_WRAP || sampler.addressV != NDb::TEXTUREADDRESSTYPE_WRAP ||
		sampler.minFilter != NDb::MINFILTERTYPE_LINEAR || sampler.magFilter != NDb::MAGFILTERTYPE_LINEAR) return nullptr;
	// Selected malformed state must reach Draw's rejection, never the opaque preview fallback.
	return &m;
}
/** Known non-Off blends are unsupported; an unknown enum must fail closed after selection. */
bool UnsupportedBakedBlend(NDb::BlendMode blend) {
	switch (blend) {
		case NDb::BLENDMODE_LERPBYALPHA:
		case NDb::BLENDMODE_PREMULTIPLIEDLERP:
		case NDb::BLENDMODE_ADDCOLOR:
		case NDb::BLENDMODE_ADDCOLORMULALPHA:
		case NDb::BLENDMODE_MULCOLOR:
		case NDb::BLENDMODE_MULINVCOLOR: return true;
		default: return false;
	}
}
/** Match Windows' derived normal/specular pins, not stale authored pin flags. */
bool AdmittedBakedMaterial(const NDb::BasicMaterial& m) {
	const auto& sampler = m.DiffuseMap.samplerState;
	// Nonfinite power remains selected-invalid rather than escaping to preview fallback.
	return m.LightingPinValue == NDb::LIGHTINGPIN_LIGHTINGBAKED &&
		m.SkeletalMeshPinValue == NDb::BOOLEANPIN_NONE && m.OpacityPinValue == NDb::BOOLEANPIN_NONE &&
		m.EmissivePinValue == NDb::EMISSIVEPIN_EMISSIVEOFF && m.ReflectivityPinValue == NDb::REFLECTIVITYPIN_REFLECTIVITYOFF &&
		!m.NormalMap.texture && !m.SpecularMap.texture && (!std::isfinite(m.specularPower) || m.specularPower <= .01f) &&
		!m.ignoreRenderState && !m.isBlendModeOverrided && !m.specialTransparency && !UnsupportedBakedBlend(m.renderState.blendMode) &&
		sampler.addressU == NDb::TEXTUREADDRESSTYPE_WRAP && sampler.addressV == NDb::TEXTUREADDRESSTYPE_WRAP &&
		sampler.minFilter == NDb::MINFILTERTYPE_LINEAR && sampler.magFilter == NDb::MAGFILTERTYPE_LINEAR;
}
/** Validate owned raw buffers without synthesizing editor/default lighting or retaining owners. */
bool BakedColorViews(const MeshVertexColors& colors) {
	const size_t maximum = pwshader::Limits{}.geometryBytes;
	if (!colors.numColors || colors.numColors > maximum / 4 || !colors.pVB1 || !colors.pVB2) return false;
	const size_t bytes = size_t(colors.numColors) * 4;
	if (size_t(colors.pVB1->storage.size()) != bytes || size_t(colors.pVB2->storage.size()) != bytes) return false;
	dayColors = {{colors.pVB1->storage.begin(),bytes},0,4};
	nightColors = {{colors.pVB2->storage.begin(),bytes},0,4};
	return true;
}
/** One bounded read per archive; the combined loader enforces the same limit on their sum. */
bool ReadArchive(const char* path, size_t limit, std::vector<unsigned char>& bytes, bool quiet = false) {
	const auto fail=[&](const char* message) { return quiet ? false : Reject(message); };
	CObj<Stream> stream = RootFileSystem::OpenFile(path, FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
	if (!IsValid(stream) || !stream->IsOk()) return fail("missing static shader archive");
	const int size = stream->GetSize();
	if (size <= 0 || size_t(size) > limit)
		return fail("missing or oversized static shader archive");
	try { bytes.resize(size_t(size)); }
	catch (const std::bad_alloc&) { return fail("static shader archive allocation failed"); }
	if (stream->Read(bytes.data(),bytes.size()) != size) return fail("short static shader archive read");
	return true;
}
bool Ready() {
	if (!programs.statics.basicFx.simple.Empty()) return true;
	if (attempted) return false;
	attempted = true;
	pwshader::Result status;
	backend = pwshader::ShaderBackend::Create({nullptr, Current, Lookup}, {}, status);
	if (!backend) return Reject(status.message.c_str());
	std::vector<unsigned char> bytes, baked, terrain, sky;
	const size_t limit = pwshader::Limits{}.archiveBytes;
	if (!ReadArchive("Shaders/UberShaders/BasicFXMaterial.shd",limit,bytes) ||
		!ReadArchive("Shaders/UberShaders/BasicMaterial.shd",limit-bytes.size(),baked)) return false;
	if (ReadArchive("Shaders/UberShaders/TerrainElementMaterial.shd",limit-bytes.size()-baked.size(),terrain,true)) {
		if (ReadArchive("Shaders/UberShaders/SkyMaterial.shd",limit-bytes.size()-baked.size()-terrain.size(),sky,true))
			status=backend->LoadSceneFamily({bytes.data(),bytes.size()},{baked.data(),baked.size()},
				{terrain.data(),terrain.size()},{sky.data(),sky.size()},programs);
		else status={pwshader::Code::InvalidInput,"sky archive missing, unreadable or oversized"};
		if (!status) {
			fprintf(stderr,"Native sky shaders unavailable: %s; retaining terrain/static shaders\n",status.message.c_str());
			status=backend->LoadSceneFamily({bytes.data(),bytes.size()},{baked.data(),baked.size()},
				{terrain.data(),terrain.size()},programs);
		}
	}
	else status = {pwshader::Code::InvalidInput,"terrain archive missing, unreadable or oversized"};
	if (!status) {
		fprintf(stderr,"Native terrain shaders unavailable: %s; retaining static shaders\n",status.message.c_str());
		status = backend->LoadStaticFamily({bytes.data(),bytes.size()},{baked.data(),baked.size()},programs.statics);
	}
	if (!status) return Reject(status.message.c_str());
	++statistics.loads;
	return true;
}
}

bool BeginContext() {
	if (backend || !glXGetCurrentContext() || lifetime == std::numeric_limits<uint64_t>::max()) return false;
	++lifetime;
	LinuxPreviewSampler::Clear();
	attempted = false;
	transparentDrawing = false;
	ClearSelection();
	return true;
}
bool Shutdown() {
	if (!LinuxPreviewSampler::Shutdown()) return false;
	transparentDrawing = false;
	ClearSelection();
	if (backend) {
		const auto result = backend->Shutdown();
		if (!result) return Reject(result.message.c_str());
		fprintf(stdout, "Native BasicFX shutdown: loads=%llu draws=%llu rejected=%llu liveBytes=%lu\n",
			static_cast<unsigned long long>(statistics.loads), static_cast<unsigned long long>(statistics.draws),
			static_cast<unsigned long long>(statistics.rejected),
			static_cast<unsigned long>(backend->GetDiagnostics().liveBytes));
		backend.reset();
	}
	programs = {};
	attempted = false;
	return true;
}
void ClearSelection() {
	selected = false; selectedRasterValid = false; selectedOpacityValid = false; selectedTexture = 0;
	selectedBaked = false; selectedBakedValid = false; dayColors = {}; nightColors = {}; night = {};
	selectedSky = false; selectedSkyValid = false; selectedMipLevels = 1;
}
bool IsSelected() { return selected; }
Statistics GetStatistics() { return statistics; }
bool IsTransparentMaterial(const NDb::Material* material) {
	const auto* m = AdmittedMaterial(material);
	return m && m->renderState.blendMode != NDb::BLENDMODE_OFF;
}
void SetTransparentDrawingEnabled(bool enabled) { ClearSelection(); transparentDrawing = enabled; }

void Select(const NDb::Material* material, const Texture* texture) {
	ClearSelection();
	if (!material || material->GetObjectTypeID() != NDb::BasicFXMaterial::typeId) return;
	const auto* admitted = AdmittedMaterial(material);
	if (!admitted) { ++statistics.unsupported; return; }
	const auto& m = *admitted;
	selected = true;
	selectedBlend = m.renderState.blendMode;
	// Authored opacity pins do not imply runtime ModifyOpacity or a different blend state.
	selectedOpacity = m.OpacityAndEmissiveModePinValue;
	const float opacity = m.opacity.GetValue(0);
	selectedOpacityValid = std::isfinite(opacity) && std::isfinite(m.masterOpacity);
	opacityAndOthers = {};
	if (selectedOpacity == NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY ||
		selectedOpacity == NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY) {
		const double product = static_cast<double>(opacity) * m.masterOpacity;
		selectedOpacityValid = selectedOpacityValid && std::isfinite(product) &&
			std::fabs(product) <= std::numeric_limits<float>::max();
		if (selectedOpacityValid) opacityAndOthers[0] = static_cast<float>(product);
	} else if (selectedOpacity != NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE) selectedOpacityValid = false;
	// Malformed states stay selected so Draw rejects instead of using the old preview.
	selectedRasterValid =
		(m.renderState.alphaTest == NDb::ONOFFSTATE_OFF || m.renderState.alphaTest == NDb::ONOFFSTATE_ON) &&
		(m.renderState.culling == NDb::ONOFFSTATE_OFF || m.renderState.culling == NDb::ONOFFSTATE_ON) &&
		m.renderState.alphaTestRef >= 0 && m.renderState.alphaTestRef <= 255;
	alphaTest = m.renderState.alphaTest == NDb::ONOFFSTATE_ON ? pwshader::AlphaTest::GreaterEqual : pwshader::AlphaTest::Disabled;
	cullMode = m.renderState.culling == NDb::ONOFFSTATE_ON ? pwshader::CullMode::Back : pwshader::CullMode::Disabled;
	alphaReference = m.renderState.alphaTestRef;
	++statistics.selections;
	selectedTexture = texture ? texture->GetOpenGLTexture() : 0;
	const auto mul = m.DiffuseMul.GetValue(0), plus = m.DiffuseAdd.GetValue(0);
	multiply = {mul.R, mul.G, mul.B, mul.A};
	add = {plus.R, plus.G, plus.B, plus.A};
	uv = {m.uOffset.GetValue(0), m.vOffset.GetValue(0) + 1.f, 0, 0};
}

void SelectBaked(const NDb::Material* material, const Texture* texture,
	const MeshVertexColors* colors, float nightFraction) {
	if (!material || material->GetObjectTypeID() != NDb::BasicMaterial::typeId) return;
	ClearSelection();
	const auto& m = *static_cast<const NDb::BasicMaterial*>(material);
	if (!colors || !AdmittedBakedMaterial(m)) { ++statistics.unsupported; ++statistics.bakedUnsupported; return; }
	selected = selectedBaked = true;
	selectedBlend = m.renderState.blendMode;
	selectedOpacityValid = true;
	selectedRasterValid = selectedBlend == NDb::BLENDMODE_OFF &&
		(m.renderState.alphaTest == NDb::ONOFFSTATE_OFF || m.renderState.alphaTest == NDb::ONOFFSTATE_ON) &&
		(m.renderState.culling == NDb::ONOFFSTATE_OFF || m.renderState.culling == NDb::ONOFFSTATE_ON) &&
		m.renderState.alphaTestRef >= 0 && m.renderState.alphaTestRef <= 255;
	alphaTest = m.renderState.alphaTest == NDb::ONOFFSTATE_ON ? pwshader::AlphaTest::GreaterEqual : pwshader::AlphaTest::Disabled;
	cullMode = m.renderState.culling == NDb::ONOFFSTATE_ON ? pwshader::CullMode::Back : pwshader::CullMode::Disabled;
	alphaReference = m.renderState.alphaTestRef;
	selectedTexture = texture ? texture->GetOpenGLTexture() : 0;
	// Original BasicMaterial24 does not consume either output constant's alpha component.
	multiply = {m.outputMul.R,m.outputMul.G,m.outputMul.B,1};
	add = {m.outputAdd.R,m.outputAdd.G,m.outputAdd.B,0};
	night = {nightFraction,0,0,0};
	selectedBakedValid = BakedColorViews(*colors) && std::isfinite(nightFraction) && std::isfinite(m.specularPower);
	for (size_t channel = 0; channel < 3; ++channel)
		selectedBakedValid = selectedBakedValid && std::isfinite(multiply[channel]) && std::isfinite(add[channel]);
	++statistics.selections; ++statistics.bakedSelections;
}

/** Validate before original iterator-based curve sampling, including unused UV components. */
template<class Curve>
static bool SampleSkyCurve(const Curve& curve, float elapsed, float& output) {
	if (curve.Count()<1 || curve.Count()>4096 || !std::isfinite(curve.GetLength()) || curve.GetLength()<0) return false;
	float previous=-1;
	for (auto it=curve.begin(); it!=curve.end(); ++it) {
		if (!std::isfinite(it->key) || !std::isfinite(it->value) || it->key<0 || it->key<=previous) return false;
		previous=it->key;
	}
	if (curve.begin()->key!=0 || (curve.IsAnimated() && previous!=curve.GetLength())) return false;
	auto left=curve.begin(), right=left;
	const float key=curve.ShiftKey(elapsed,0,&left,&right);
	output=curve.GetValue(key,left,right);
	return std::isfinite(output);
}

void SelectSky(const NDb::SkyMaterial& m, uint32_t texture, unsigned mipLevels, float elapsed) {
	ClearSelection(); selected=selectedSky=true; ++statistics.selections;
	selectedTexture=texture; selectedMipLevels=mipLevels; selectedBlend=m.renderState.blendMode;
	selectedOpacityValid=true;
	const auto& sampler=m.DiffuseMap.samplerState;
	selectedRasterValid=!m.ignoreRenderState && m.renderState.blendMode==NDb::BLENDMODE_OFF &&
		(m.renderState.alphaTest==NDb::ONOFFSTATE_OFF || m.renderState.alphaTest==NDb::ONOFFSTATE_ON) &&
		(m.renderState.culling==NDb::ONOFFSTATE_OFF || m.renderState.culling==NDb::ONOFFSTATE_ON) &&
		m.renderState.alphaTestRef>=0 && m.renderState.alphaTestRef<=255;
	alphaTest=m.renderState.alphaTest==NDb::ONOFFSTATE_ON?pwshader::AlphaTest::GreaterEqual:pwshader::AlphaTest::Disabled;
	cullMode=m.renderState.culling==NDb::ONOFFSTATE_ON?pwshader::CullMode::Back:pwshader::CullMode::Disabled;
	alphaReference=m.renderState.alphaTestRef;
	multiply={m.outputMul.R,m.outputMul.G,m.outputMul.B,1}; add={m.outputAdd.R,m.outputAdd.G,m.outputAdd.B,0};
	selectedSkyValid=sampler.addressU==NDb::TEXTUREADDRESSTYPE_WRAP && sampler.addressV==NDb::TEXTUREADDRESSTYPE_WRAP &&
		sampler.minFilter==NDb::MINFILTERTYPE_LINEAR && sampler.magFilter==NDb::MAGFILTERTYPE_LINEAR &&
		sampler.mipFilter==NDb::MIPFILTERTYPE_LINEAR && std::isfinite(elapsed) && elapsed>=0 &&
		SampleSkyCurve(m.uOffset1,elapsed,uv[0]) && SampleSkyCurve(m.vOffset1,elapsed,uv[1]) &&
		SampleSkyCurve(m.uOffset2,elapsed,uv[2]) && SampleSkyCurve(m.vOffset2,elapsed,uv[3]);
}

bool Draw(pwshader::IndexedDraw draw, const Matrix43* world, const pwshader::PreviewTransform& preview) {
	if (!selected || !world) return Reject("missing selected material or WORLD");
	if (!selectedRasterValid) return Reject("invalid selected alpha-test or culling state");
	if (!selectedOpacityValid) return Reject("invalid selected constant opacity or permutation");
	if (selectedSky && (!selectedSkyValid || GetRuntimePins().InstancingValue!=NDb::BOOLEANPIN_NONE ||
		GetRuntimePins().RenderModeValue!=NDb::RENDERMODEPIN_RENDERNORMAL)) return Reject("invalid sky curve/sampler or unsupported runtime permutation");
	if (selectedBaked && (!selectedBakedValid || GetRuntimePins().InstancingValue != NDb::BOOLEANPIN_NONE ||
		GetRuntimePins().RenderModeValue != NDb::RENDERMODEPIN_RENDERNORMAL))
		return Reject("invalid baked colours/constants or unsupported runtime permutation");
	if (selectedBlend != NDb::BLENDMODE_OFF && !transparentDrawing)
		return Reject("transparent material outside the retained view pass");
	if (GetRuntimePins().DoDepthOutValue != NDb::BOOLPIN_FALSE)
		return Reject("BasicFX opacity family cannot render depth-output permutation");
	std::array<float, 16> modelView{}, projection{};
	glGetFloatv(GL_MODELVIEW_MATRIX, modelView.data());
	glGetFloatv(GL_PROJECTION_MATRIX, projection.data());
	const std::array<float, 16> rows = {world->_11, world->_12, world->_13, world->_14,
		world->_21, world->_22, world->_23, world->_24,
		world->_31, world->_32, world->_33, world->_34, 0, 0, 0, 1};
	pwshader::BasicFxTransformRegisters registers;
	if (!pwshader::BuildBasicFxTransformRegisters(rows, modelView, projection, preview, registers))
		return Reject("invalid WORLD or preview camera");
	if (!Ready()) return false;
	const pwshader::ProgramRef* program = &programs.statics.basicFx.simple;
	if (selectedSky) program = &programs.sky;
	else if (selectedBaked) program = &programs.statics.bakedBasicMaterial;
	else if (selectedOpacity == NDb::OPACITYANDEMISSIVEMODEPIN_ADDITIVEOPACITY) program = &programs.statics.basicFx.additiveOpacity;
	else if (selectedOpacity == NDb::OPACITYANDEMISSIVEMODEPIN_BLENDOPACITY) program = &programs.statics.basicFx.blendOpacity;
	for (const auto& result : {
		backend->SetFloat4(pwshader::Stage::Vertex, 0, {registers.world.data(), selectedBaked || selectedSky ? size_t(3) : size_t(4)}),
		backend->SetFloat4(pwshader::Stage::Vertex, 8, {registers.viewProjection.data(), 4}),
		backend->SetFloat4(pwshader::Stage::Pixel, 20, {&multiply, 1}),
		backend->SetFloat4(pwshader::Stage::Pixel, 21, {&add, 1})})
		if (!result) return Reject(result.message.c_str());
	if (selectedBaked) {
		const auto result = backend->SetFloat4(pwshader::Stage::Vertex,234,{&night,1});
		if (!result) return Reject(result.message.c_str());
		draw.dayColors = dayColors; draw.nightColors = nightColors;
	} else {
		const auto result=backend->SetFloat4(pwshader::Stage::Vertex,20,{&uv,1});
		if (!result) return Reject(result.message.c_str());
		if (!selectedSky) {
			const auto opacityResult=backend->SetFloat4(pwshader::Stage::Pixel,6,{&opacityAndOthers,1});
			if (!opacityResult) return Reject(opacityResult.message.c_str());
		}
	}
	GLint viewport[4]{};
	glGetIntegerv(GL_VIEWPORT, viewport);
	draw.viewportX = viewport[0]; draw.viewportY = viewport[1];
	draw.viewportWidth = viewport[2]; draw.viewportHeight = viewport[3];
	draw.texture = selectedTexture;
	draw.textureMipLevels = selectedMipLevels;
	// The orthographic preview can sit outside the authored sky enclosure. Keep its
	// original geometry/UVs, but reserve sky fragments for untouched background depth.
	draw.skyAtFarDepth = selectedSky;
	draw.alphaTest = alphaTest;
	draw.cullMode = cullMode;
	draw.alphaReference = alphaReference;
	switch (selectedBlend) {
		case NDb::BLENDMODE_OFF: draw.blendMode = pwshader::BlendMode::Off; break;
		case NDb::BLENDMODE_LERPBYALPHA: draw.blendMode = pwshader::BlendMode::LerpByAlpha; break;
		case NDb::BLENDMODE_PREMULTIPLIEDLERP: draw.blendMode = pwshader::BlendMode::PremultipliedLerp; break;
		case NDb::BLENDMODE_ADDCOLOR: draw.blendMode = pwshader::BlendMode::AddColor; break;
		case NDb::BLENDMODE_ADDCOLORMULALPHA: draw.blendMode = pwshader::BlendMode::AddColorMulAlpha; break;
		case NDb::BLENDMODE_MULCOLOR: draw.blendMode = pwshader::BlendMode::MulColor; break;
		case NDb::BLENDMODE_MULINVCOLOR: draw.blendMode = pwshader::BlendMode::MulInvColor; break;
		default: return Reject("invalid selected blend mode");
	}
	// Retain preview winding; original perspective-camera presentation is separate.
	draw.frontFace = pwshader::Winding::CounterClockwise;
	const auto result = backend->Draw(*program, draw);
	if (!result) return Reject(result.message.c_str());
	++statistics.draws;
	if (selectedBaked) ++statistics.bakedDraws;
	if (selectedSky) ++statistics.skyDraws;
	return true;
}

bool DrawTerrain(pwshader::IndexedDraw draw, const TerrainConstants& constants,
	const pwshader::PreviewTransform& preview) {
	ClearSelection();
	const auto reject=[](const char* message) {
		++statistics.rejected;
		if (++statistics.terrainRejected==1) fprintf(stderr,"Native terrain draw rejected: %s\n",message);
		return false;
	};
	if (constants.layers<1 || constants.layers>4) return reject("terrain layer count outside 1..4");
	std::array<float,16> modelView{},projection{};
	glGetFloatv(GL_MODELVIEW_MATRIX,modelView.data()); glGetFloatv(GL_PROJECTION_MATRIX,projection.data());
	const std::array<float,16> identity{{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
	pwshader::BasicFxTransformRegisters transform;
	if (!pwshader::BuildBasicFxTransformRegisters(identity,modelView,projection,preview,transform))
		return reject("invalid terrain camera transform");
	if (!Ready()) return false;
	const auto& program=programs.terrain[(constants.layers-1)*2+unsigned(constants.localUV)];
	if (program.Empty()) return reject("ordinary terrain family unavailable");
	const pwshader::Float4 tiling{constants.tiling,0,0,0}, night{constants.nightFraction,0,0,0}, burn{constants.overburn,0,0,0};
	for (const auto& status : {
		backend->SetFloat4(pwshader::Stage::Vertex,8,{transform.viewProjection.data(),4}),
		backend->SetFloat4(pwshader::Stage::Vertex,20,{&tiling,1}),
		backend->SetFloat4(pwshader::Stage::Vertex,21,{&constants.natureMapping,1}),
		backend->SetFloat4(pwshader::Stage::Vertex,234,{&night,1}),
		backend->SetFloat4(pwshader::Stage::Pixel,20,{&burn,1}),
		backend->SetFloat4(pwshader::Stage::Pixel,21,{&constants.natureWeight,1})})
		if (!status) return reject(status.message.c_str());
	GLint viewport[4]{}; glGetIntegerv(GL_VIEWPORT,viewport);
	draw.viewportX=viewport[0]; draw.viewportY=viewport[1];
	draw.viewportWidth=viewport[2]; draw.viewportHeight=viewport[3];
	// Original terrain triangles face +worldZ. The preview's XYZ->XZY mapping reverses winding.
	draw.frontFace=pwshader::Winding::Clockwise;
	const auto result=backend->Draw(program,draw);
	if (!result) return reject(result.message.c_str());
	++statistics.draws; ++statistics.terrainDraws;
	return true;
}

bool SupportsTerrainAdditive() {
	return Ready() && !programs.terrain[0].Empty() && backend->SupportsIndexedBlending();
}
}}
#endif
