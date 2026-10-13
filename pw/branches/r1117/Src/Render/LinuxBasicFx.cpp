#include "stdafx.h"
#include "LinuxBasicFx.h"

#if defined(PW_LINUX_OPENGL_BOOTSTRAP)
#include "DBRender.h"
#include "MaterialSpec.h"
#include "texture.h"
#include "../System/FileSystem/FileSystem.h"
#include "../System/Stream.h"
#include "../System/matrix43.h"
#include <GL/gl.h>
#include <GL/glx.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace Render { namespace LinuxBasicFx {
namespace {
uint64_t lifetime = 0;
std::unique_ptr<pwshader::ShaderBackend> backend;
pwshader::ProgramRef program;
Statistics statistics;
bool selected = false, attempted = false;
bool selectedRasterValid = false;
pwshader::AlphaTest alphaTest = pwshader::AlphaTest::Disabled;
pwshader::CullMode cullMode = pwshader::CullMode::Back;
int alphaReference = 127;
unsigned int selectedTexture = 0;
pwshader::Float4 uv{}, multiply{}, add{};

pwshader::ContextKey Current(void*) noexcept {
	return {reinterpret_cast<uintptr_t>(glXGetCurrentContext()), lifetime};
}
void* Lookup(const char* name, void*) {
	return reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
}
bool Reject(const char* message) {
	if (++statistics.rejected == 1)
		fprintf(stderr, "Native BasicFX draw rejected: %s\n", message);
	return false;
}
bool Ready() {
	if (!program.Empty()) return true;
	if (attempted) return false;
	attempted = true;
	pwshader::Result status;
	backend = pwshader::ShaderBackend::Create({nullptr, Current, Lookup}, {}, status);
	if (!backend) return Reject(status.message.c_str());
	CObj<Stream> stream = RootFileSystem::OpenFile("Shaders/UberShaders/BasicFXMaterial.shd",
		FILEACCESS_READ, FILEOPEN_OPEN_EXISTING);
	if (!IsValid(stream) || !stream->IsOk() || stream->GetSize() <= 0 || stream->GetSize() > 4 * 1024 * 1024)
		return Reject("missing or oversized BasicFX shader archive");
	std::vector<unsigned char> bytes(stream->GetSize());
	if (stream->Read(bytes.data(), bytes.size()) != static_cast<int>(bytes.size()))
		return Reject("short BasicFX shader archive read");
	status = backend->LoadBasicFxPair0({bytes.data(), bytes.size()}, program);
	if (!status) return Reject(status.message.c_str());
	++statistics.loads;
	return true;
}
}

bool BeginContext() {
	if (backend || !glXGetCurrentContext() || lifetime == std::numeric_limits<uint64_t>::max()) return false;
	++lifetime;
	attempted = false;
	ClearSelection();
	return true;
}
bool Shutdown() {
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
	program.Reset();
	attempted = false;
	return true;
}
void ClearSelection() { selected = false; selectedRasterValid = false; selectedTexture = 0; }
bool IsSelected() { return selected; }
Statistics GetStatistics() { return statistics; }

void Select(const NDb::Material* material, const Texture* texture) {
	ClearSelection();
	if (!material || material->GetObjectTypeID() != NDb::BasicFXMaterial::typeId) return;
	const auto& m = *static_cast<const NDb::BasicFXMaterial*>(material);
	const auto& sampler = m.DiffuseMap.samplerState;
	if (m.SkeletalMeshPinValue != NDb::BOOLEANPIN_NONE ||
		m.MultiplyVertexColorPinValue != NDb::BOOLEANPIN_NONE ||
		m.OpacityAndEmissiveModePinValue != NDb::OPACITYANDEMISSIVEMODEPIN_SIMPLE ||
		m.DistortionPinValue != NDb::DISTORTIONPIN_DISTORTNONE || m.ignoreRenderState ||
		m.renderState.blendMode != NDb::BLENDMODE_OFF || m.isBlendModeOverrided ||
		m.opacity.IsAnimated() || m.opacity.GetValue(0) != 1 || m.masterOpacity != 1 ||
		m.DiffuseMul.IsAnimated() || m.DiffuseAdd.IsAnimated() || m.uOffset.IsAnimated() || m.vOffset.IsAnimated() ||
		sampler.addressU != NDb::TEXTUREADDRESSTYPE_WRAP || sampler.addressV != NDb::TEXTUREADDRESSTYPE_WRAP ||
		sampler.minFilter != NDb::MINFILTERTYPE_LINEAR || sampler.magFilter != NDb::MAGFILTERTYPE_LINEAR) {
		++statistics.unsupported;
		return;
	}
	selected = true;
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

bool Draw(pwshader::IndexedDraw draw, const Matrix43* world, const pwshader::PreviewTransform& preview) {
	if (!selected || !world) return Reject("missing selected material or WORLD");
	if (!selectedRasterValid) return Reject("invalid selected alpha-test or culling state");
	if (GetRuntimePins().DoDepthOutValue != NDb::BOOLPIN_FALSE)
		return Reject("BasicFX pair zero cannot render depth-output permutation");
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
	for (const auto& result : {
		backend->SetFloat4(pwshader::Stage::Vertex, 0, {registers.world.data(), 4}),
		backend->SetFloat4(pwshader::Stage::Vertex, 8, {registers.viewProjection.data(), 4}),
		backend->SetFloat4(pwshader::Stage::Vertex, 20, {&uv, 1}),
		backend->SetFloat4(pwshader::Stage::Pixel, 20, {&multiply, 1}),
		backend->SetFloat4(pwshader::Stage::Pixel, 21, {&add, 1})})
		if (!result) return Reject(result.message.c_str());
	GLint viewport[4]{};
	glGetIntegerv(GL_VIEWPORT, viewport);
	draw.viewportX = viewport[0]; draw.viewportY = viewport[1];
	draw.viewportWidth = viewport[2]; draw.viewportHeight = viewport[3];
	draw.texture = selectedTexture;
	draw.alphaTest = alphaTest;
	draw.cullMode = cullMode;
	draw.alphaReference = alphaReference;
	// Use the tested native preview winding, without a per-object determinant flip.
	draw.frontFace = pwshader::Winding::CounterClockwise;
	const auto result = backend->Draw(program, draw);
	if (!result) return Reject(result.message.c_str());
	++statistics.draws;
	return true;
}
}}
#endif
