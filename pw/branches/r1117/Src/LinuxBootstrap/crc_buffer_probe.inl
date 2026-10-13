/** Exercise the actual transceiver byte owner, separately covering both release sites.
 * ASan must also run these cases: scalar deletion of a byte array can appear to
 * succeed with an ordinary allocator. No CRC serialization or cache policy changes.
 */
static bool RunLinuxCrcBufferProbe(bool reallocate)
{
	unsigned checks = 0, failures = 0;
	const auto check = [&](bool value, const char* label) {
		++checks;
		if (!value) { ++failures; fprintf(stderr, "CRC buffer: %s\n", label); }
	};
	const auto allocate = [&](NCore::TransceiverCrcCalculator::Buffer& buffer, size_t size) {
		buffer.Allocate(size);
		check(buffer.buffer != 0, "owned allocation");
		check(buffer.size == size, "requested capacity");
		check(buffer.length == 0, "allocation resets used length");
		std::fill_n(buffer.buffer, size, static_cast<unsigned char>(0xa5));
		check(std::all_of(buffer.buffer, buffer.buffer + size,
			[](unsigned char value) { return value == 0xa5; }), "entire allocation writable");
		buffer.length = size;
	};
	if (reallocate) {
		NCore::TransceiverCrcCalculator::Buffer buffer;
		for (size_t size : {64u, 4096u, 4096u, 1u, 0u, 64u}) allocate(buffer, size);
	} else {
		{ NCore::TransceiverCrcCalculator::Buffer empty;
			check(!empty.buffer && !empty.size && !empty.length, "empty owner"); }
		for (size_t size : {0u, 1u, 64u, 4096u, 1048576u}) {
			NCore::TransceiverCrcCalculator::Buffer buffer;
			allocate(buffer, size);
		}
	}
	fprintf(stdout, "CRC buffer %s: %u checks, %u failures\n",
		reallocate ? "reallocation" : "destruction", checks, failures);
	return failures == 0;
}
