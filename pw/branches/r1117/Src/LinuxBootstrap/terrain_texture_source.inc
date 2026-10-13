/** Read one bounded immutable DDS source; callers admit its cost before retaining it. */
static LinuxBootstrap::DdsMipStatus ReadLinuxDdsMipSource(const fs::path& path,
	std::shared_ptr<const std::vector<unsigned char>>& source, LinuxBootstrap::DdsMipChain& chain)
{
	using LinuxBootstrap::DdsMipStatus;
	source.reset(); chain={};
	std::error_code error;
	const auto size=fs::file_size(path,error);
	if (error || size<128 || size>64*1024*1024) return DdsMipStatus::Invalid;
	try {
		auto bytes=std::make_shared<std::vector<unsigned char>>(size_t(size));
		std::ifstream stream(path,std::ios::binary);
		if (!stream.read(reinterpret_cast<char*>(bytes->data()),std::streamsize(bytes->size())) ||
			stream.peek()!=std::char_traits<char>::eof()) return DdsMipStatus::Invalid;
		const auto status=LinuxBootstrap::InspectDdsMipChain(bytes->data(),bytes->size(),chain);
		if (status==DdsMipStatus::Valid) source=std::move(bytes);
		return status;
	} catch (const std::bad_alloc&) { return DdsMipStatus::Invalid; }
}
