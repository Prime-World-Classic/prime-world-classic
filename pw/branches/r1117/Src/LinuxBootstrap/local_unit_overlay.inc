/** Compact live health/selection indicators; diagnostic pedestals/arrows remain debug-only.
 * Billboard dimensions are pixels, independent of map zoom. Missing meshes keep an
 * explicit small world-space proxy until the corresponding authored unit renderer exists.
 */
static bool DrawLinuxLocalUnitOverlay(const NWorld::LinuxDynamicWorldMarker& marker,
	float x, float z, float ground, float meshHeight, float scale, bool selected, bool missing,
	size_t& healthBars, size_t& selectedBadges)
{
	if (marker.dead) return false;
	GLdouble model[16], projection[16];
	GLint viewport[4];
	glGetDoublev(GL_MODELVIEW_MATRIX, model);
	glGetDoublev(GL_PROJECTION_MATRIX, projection);
	glGetIntegerv(GL_VIEWPORT, viewport);
	const double top = ground + meshHeight + .45f * scale;
	const double ex = model[0] * x + model[4] * top + model[8] * z + model[12];
	const double ey = model[1] * x + model[5] * top + model[9] * z + model[13];
	const double ez = model[2] * x + model[6] * top + model[10] * z + model[14];
	if (viewport[2] <= 0 || viewport[3] <= 0 || projection[0] <= 0 || projection[5] <= 0 ||
		std::abs(ex * projection[0]) > 1.1 || std::abs(ey * projection[5]) > 1.1 || std::abs(ez) > 220.) return false;
	unsigned char red = 0, green = 0, blue = 0;
	ResolveLinuxDynamicWorldMarkerColor(marker, &red, &green, &blue);
	if (missing)
		DrawLinuxBootstrap3DCuboid(x, ground, z, scale, scale, meshHeight, red, green, blue, 160);
	if (selected) {
		SetOpenGlColor(220, 235, 130, 230);
		glLineWidth(1.5f);
		glBegin(GL_LINE_LOOP);
		for (int i = 0; i < 32; ++i) {
			const float angle = i * (6.28318530718f / 32);
			glVertex3f(x + cosf(angle) * 2.f * scale, ground + .08f * scale,
				z + sinf(angle) * 2.f * scale);
		}
		glEnd();
		glLineWidth(1.f);
		++selectedBadges;
	}
	const double px = 2. / (projection[0] * viewport[2]);
	const double py = 2. / (projection[5] * viewport[3]);
	const bool hero = marker.kind == NWorld::LinuxDynamicWorldMarker::KIND_HERO;
	const double half = (hero ? 22. : 15.) * px;
	glPushMatrix();
	glLoadIdentity();
	const auto bar = [&](double offset, float value, unsigned char r, unsigned char g, unsigned char b) {
		const double bottom = ey + offset * py, topEdge = bottom + 4. * py;
		SetOpenGlColor(12, 16, 18, 240);
		glBegin(GL_QUADS);
		glVertex3d(ex - half - px, bottom - py, ez); glVertex3d(ex + half + px, bottom - py, ez);
		glVertex3d(ex + half + px, topEdge + py, ez); glVertex3d(ex - half - px, topEdge + py, ez);
		glEnd();
		SetOpenGlColor(r, g, b, 245);
		const double right = ex - half + half * 2. * ClampLinuxDynamicMarkerPercent(value);
		glBegin(GL_QUADS);
		glVertex3d(ex - half, bottom, ez); glVertex3d(right, bottom, ez);
		glVertex3d(right, topEdge, ez); glVertex3d(ex - half, topEdge, ez);
		glEnd();
	};
	bar(0, marker.healthPercent, 85, 209, 86);
	if (hero) bar(-6, marker.energyPercent, 67, 153, 225);
	glPopMatrix();
	++healthBars;
	return true;
}
