/** Exercise the real resource-path helpers, including the Flash loader's in-place call.
 * Paths deliberately keep legacy slash, drive, case and dot-segment behavior. Run
 * under ASan as well: overlapping memcpy may appear correct in an ordinary build.
 */
static bool RunLinuxFilePathProbe()
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool value, const char* label) {
		++checks;
		if (!value) { ++failures; fprintf(stderr, "Resource path: %s\n", label); }
	};
	struct Case { const char* source; const char* parent; const char* expected; };
	const auto run = [&](const Case& fixture, bool relative) {
		const auto convert = relative ? NFile::MakeRelativePath : NFile::MakeFullPath;
		for (unsigned alias = 0; alias < 3; ++alias) {
			nstl::string source(fixture.source), parent(fixture.parent), output("previous-output");
			nstl::string* result = alias == 1 ? &source : alias == 2 ? &parent : &output;
			convert(result, source, parent);
			check(*result == fixture.expected, relative ? "relative conversion/alias" : "full conversion/alias");
			check(alias == 1 || source == fixture.source, "non-output source preserved");
			check(alias == 2 || parent == fixture.parent, "non-output parent preserved");
		}
	};
	for (const auto& fixture : {
		Case{"/UI/Content/Loading/map_modes.dds", "UI/Flash/Loading.swf", "UI/Content/Loading/map_modes.dds"},
		Case{"\\UI\\image.dds", "UI/Flash/root.swf", "UI\\image.dds"},
		Case{"image.dds", "UI/Flash/root.swf", "UI/Flash/image.dds"},
		Case{"image.dds", "UI\\Flash\\root.swf", "UI\\Flash/image.dds"},
		Case{"../image.dds", "UI/Flash/root.swf", "UI/Flash/../image.dds"},
		Case{"image.dds", "root.swf", "image.dds"},
		Case{"C:\\image.dds", "UI/root.swf", "UI/C:\\image.dds"},
		Case{"image.dds", "", "image.dds"}, Case{"/image.dds", "", "image.dds"},
		Case{"//image.dds", "UI/root.swf", "/image.dds"}, Case{"/", "UI/root.swf", ""},
		Case{"\\", "", ""}, Case{"", "UI/root.swf", ""}, Case{"", "", ""}})
		run(fixture, false);
	for (const auto& fixture : {
		Case{"UI/Flash/image.dds", "UI/Flash/root.swf", "image.dds"},
		Case{"ui\\flash\\image.dds", "UI/Flash/root.swf", "image.dds"},
		Case{"UI/Flash/sub/image.dds", "UI/Flash/root.swf", "sub/image.dds"},
		Case{"UI/image.dds", "Other/root.swf", "/UI/image.dds"},
		Case{"UI/Flash/", "UI/Flash/root.swf", ""},
		Case{"image.dds", "root.swf", "/image.dds"},
		Case{"/image.dds", "", "//image.dds"}, Case{"", "UI/root.swf", ""}, Case{"", "", ""}})
		run(fixture, true);
	for (bool relative : {false, true}) {
		nstl::string both("UI/root.swf");
		const auto convert = relative ? NFile::MakeRelativePath : NFile::MakeFullPath;
		convert(&both, both, both);
		check(both == (relative ? "root.swf" : "UI/UI/root.swf"), "all three arguments alias");
	}
	// Match the old pointer-taking overload's embedded-NUL truncation exactly.
	const char bytes[] = {'/', 'a', '\0', 'b'};
	nstl::string binary(bytes, sizeof(bytes));
	NFile::MakeFullPath(&binary, binary, nstl::string("UI/root.swf"));
	check(binary == "a", "leading-separator conversion retains C-string semantics");
	std::string longName(4096, 'x');
	nstl::string longPath(("/UI/" + longName).c_str());
	NFile::MakeFullPath(&longPath, longPath, nstl::string("parent.swf"));
	check(longPath == ("UI/" + longName).c_str(), "large in-place path");
	fprintf(stdout, "Resource paths: %u checks, %u failures\n", checks, failures);
	return failures == 0;
}
