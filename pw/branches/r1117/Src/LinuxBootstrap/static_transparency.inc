/** One view retains deferred static meshes until its explicit transparent flush.
 * Existing BatchQueue sorting is retained; no pointers into temporary queues escape.
 */
class LinuxStaticTransparentPass
{
public:
	struct Counts {
		size_t payloads = 0, batches = 0, triangles = 0, rejected = 0;
		LinuxRendererMaterialSlotStats materials;
		void Accumulate(size_t& payloadCount, size_t& batchCount, size_t& triangleCount,
			LinuxRendererMaterialSlotStats& materialCount) const {
			payloadCount += payloads; batchCount += batches; triangleCount += triangles;
			materialCount.authoredMaterials += materials.authoredMaterials;
			materialCount.diffuseSamplers += materials.diffuseSamplers;
			materialCount.diffuseTextures += materials.diffuseTextures;
			materialCount.fallbackMaterials += materials.fallbackMaterials;
			materialCount.smartRendererBinds += materials.smartRendererBinds;
			materialCount.manualFallbackBinds += materials.manualFallbackBinds;
			materialCount.manualFallbackOnlyBatches += materials.manualFallbackOnlyBatches;
			materialCount.missingGL += materials.missingGL;
		}
	};
	struct Owner {
		std::shared_ptr<Render::StaticMesh> mesh;
		unsigned group = 0;
		bool counted = false, depthValid = false;
		float depth = 0;
	};

private:
	class CountedPrimitive : public Render::IRenderablePrimitive
	{
		LinuxStaticTransparentPass& pass;
		Owner& owner;
		const Render::IRenderablePrimitive* primitive;
		Render::BaseMaterial* material;
		size_t textureIndex;
	public:
		CountedPrimitive(LinuxStaticTransparentPass& p, Owner& o, const Render::Batch& batch, size_t texture)
			: pass(p), owner(o), primitive(batch.pPrimitive), material(batch.pMaterial), textureIndex(texture) {}
		void Bind() const override { primitive->Bind(); }
		Render::GeometryBuffers GetBuffers() const override { return primitive->GetBuffers(); }
		Render::GeometryStatistics GetStats() const override { return primitive->GetStats(); }
		void Draw() const override {
			const auto before = Render::LinuxBasicFx::GetStatistics();
			primitive->Draw();
			const auto after = Render::LinuxBasicFx::GetStatistics();
			auto& count = pass.counts[owner.group];
			if (after.draws > before.draws) {
				++count.batches; count.triangles += primitive->GetStats().numTriangles;
				if (!owner.counted) { owner.counted = true; ++count.payloads; }
				RecordLinuxRendererMaterialSlotStats(material, textureIndex, &count.materials);
				RecordLinuxRendererSmartTextureBindStats(pass.previousBinds,
					textureIndex != kLinuxHeroPreviewNoDiffuseTexture, &count.materials);
			} else ++count.rejected;
			pass.previousBinds = Render::SmartRenderer::GetOpenGLTextureBindStats();
		}
	};

	std::array<Counts, 2> counts;
	std::vector<std::unique_ptr<Owner>> owners;
	std::vector<std::unique_ptr<CountedPrimitive>> primitives;
	Render::BatchQueueSorter sorter;
	Render::BatchQueue queue;
	pwshader::PreviewTransform preview;
	std::array<float, 16> modelView{}, projection{};
	std::array<GLint, 4> viewport{};
	GLint framebuffer = 0;
	size_t payloadLimit = 0, capacity = 0;
	bool flushed = false, valid = false;
	Render::SmartRenderer::OpenGLTextureBindStats previousBinds{};

	static int Capacity(size_t payloads) {
		// Match the existing native map payload budgets; reserve before publishing any batch pointers.
		return payloads <= kLinuxMapStaticPreviewMaxPayloadFiles + kLinuxMapTerrainElementPreviewMaxPayloadFiles ?
			static_cast<int>(payloads * 16) : 0;
	}
	void Clear() {
		// These setters only clear CPU selection/borrowed matrix pointers; destruction never calls GL.
		Render::LinuxBasicFx::SetTransparentDrawingEnabled(false);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		queue.Clear(); primitives.clear(); owners.clear();
	}

public:
	LinuxStaticTransparentPass(size_t payloads, pwshader::PreviewTransform transform)
		: sorter(Capacity(payloads), false), queue(Render::BatchQueue::INDEX_MAIN, sorter, Capacity(payloads)),
		preview(transform), payloadLimit(payloads), capacity(Capacity(payloads)), valid(!payloads || capacity) {
		preview.scale = preview.scale > .0001f ? preview.scale : 1.f;
		glGetFloatv(GL_MODELVIEW_MATRIX, modelView.data()); glGetFloatv(GL_PROJECTION_MATRIX, projection.data());
		glGetIntegerv(GL_VIEWPORT, viewport.data()); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
		queue.SetSorting(NDb::MATERIALPRIORITY_TRANSPARENT, true);
	}
	LinuxStaticTransparentPass(const LinuxStaticTransparentPass&) = delete;
	LinuxStaticTransparentPass& operator=(const LinuxStaticTransparentPass&) = delete;
	~LinuxStaticTransparentPass() { Clear(); }

	/** WORLD and DB bounds are copied/consumed while the submitting component is alive. */
	Owner* Retain(std::unique_ptr<Render::StaticMesh> mesh, const Matrix43& world,
		const NDb::AABB& bounds, unsigned group) {
		return Retain(std::shared_ptr<Render::StaticMesh>(std::move(mesh)), world, bounds, group);
	}

	/** A map cache may be cleared while this view still owns deferred batches. */
	Owner* Retain(std::shared_ptr<Render::StaticMesh> mesh, const Matrix43& world,
		const NDb::AABB& bounds, unsigned group) {
		if (!valid || flushed || !mesh || group >= counts.size() || owners.size() >= payloadLimit) return nullptr;
		auto owner = std::make_unique<Owner>(); owner->mesh = std::move(mesh); owner->group = group;
		const std::array<float, 16> rows{world._11,world._12,world._13,world._14,
			world._21,world._22,world._23,world._24,world._31,world._32,world._33,world._34,0,0,0,1};
		// An all-point DB default is not a substitute for missing mesh bounds; flat planes remain valid.
		owner->depthValid = (bounds.minX < bounds.maxX || bounds.minY < bounds.maxY || bounds.minZ < bounds.maxZ) &&
			pwshader::BuildBasicFxSortDepth(rows, modelView, preview,
				{bounds.minX,bounds.minY,bounds.minZ}, {bounds.maxX,bounds.maxY,bounds.maxZ}, owner->depth);
		auto* result = owner.get(); owners.push_back(std::move(owner)); return result;
	}

	/** Return true for an admitted transparent batch, even if rejected, preventing opaque fallback. */
	bool Defer(Owner& owner, const Render::Batch& batch, size_t textureIndex) {
		if (!batch.pMaterial || !Render::LinuxBasicFx::IsTransparentMaterial(batch.pMaterial->GetDBMaterial())) return false;
		if (!valid || flushed || !owner.depthValid || !batch.pPrimitive || primitives.size() >= capacity ||
			batch.pOwner != owner.mesh.get() || batch.pMaterial->GetAlternativeMaterial()) {
			++counts[owner.group].rejected; return true;
		}
		auto primitive = std::make_unique<CountedPrimitive>(*this, owner, batch, textureIndex);
		const auto* borrowed = primitive.get(); primitives.push_back(std::move(primitive));
		queue.SetCurrentSortingValue(owner.depth);
		queue.Push(NDb::MATERIALPRIORITY_TRANSPARENT, owner.mesh.get(), borrowed, batch.elementNumber, batch.pMaterial);
		return true;
	}

	/** Explicit once-only boundary after opaque geometry, before overlays. Never clears scene depth. */
	bool Flush() {
		if (flushed) return false;
		flushed = true;
		std::array<float, 16> currentView{}, currentProjection{}; std::array<GLint, 4> currentViewport{};
		GLint currentFramebuffer = 0;
		glGetFloatv(GL_MODELVIEW_MATRIX, currentView.data()); glGetFloatv(GL_PROJECTION_MATRIX, currentProjection.data());
		glGetIntegerv(GL_VIEWPORT, currentViewport.data()); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &currentFramebuffer);
		if (!valid || currentView != modelView || currentProjection != projection || currentViewport != viewport || currentFramebuffer != framebuffer) {
			for (const auto& owner : owners) ++counts[owner->group].rejected;
			Clear(); return false;
		}
		Render::SmartRenderer::SetOpenGLImmediateMeshPreviewTransform(preview.centerX, preview.centerY, preview.minZ, preview.scale);
		struct ResetOnExit {
			LinuxStaticTransparentPass& pass;
			~ResetOnExit() { pass.Clear(); }
		} reset{*this};
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(true);
		Render::LinuxBasicFx::SetTransparentDrawingEnabled(true);
		previousBinds = Render::SmartRenderer::GetOpenGLTextureBindStats();
		queue.Render(NDb::MATERIALPRIORITY_TRANSPARENT);
		Render::LinuxBasicFx::SetTransparentDrawingEnabled(false);
		Render::SmartRenderer::UnBindTexture(0u);
		Render::SmartRenderer::SetOpenGLImmediateMeshDrawingEnabled(false);
		Clear(); return true;
	}
	const Counts& GetCounts(unsigned group) const { return counts.at(group); }
};
