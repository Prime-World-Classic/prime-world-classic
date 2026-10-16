/** A real DB-registered XML resource that reports recursive deserialization safely. */
struct CacheResourceB13 : NDb::DbResource
{
	using DbResource = NDb::DbResource;
	using DBID = NDb::DBID;
	DBRESOURCE_METHODS(CacheResourceB13);
	vector<NDb::Ptr<CacheResourceB13>> children;
	NDb::Ptr<CacheResourceB13> link;
	int value = 0, reads = 0, reentries = 0;
	bool reading = false;
	int operator&(IXmlSaver& saver) override
	{
		// Report the cache defect before a recursive vector reload can invalidate this fixture.
		if (reading) { ++reentries; return 0; }
		reading = true; ++reads;
		saver.Add("children", &children); saver.Add("link", &link); saver.Add("value", &value);
		reading = false;
		return 0;
	}
	checksum_type CalcCheckSum() const override { return 0; }
};

/** Cold inline ownership and cyclic root identity use the actual cache, serializer and file system. */
static void ResourceCacheOwnershipB13(Checks& check)
{
	const int before = check.count, failed = check.failed;
	static bool registered = false;
	if (!registered)
	{
		NDb::GetDbTypesRegistrator().RegisterResoruceType("CacheResourceB13", &CacheResourceB13::NewCacheResourceB13);
		registered = true;
	}
	CObj<TestFileSystem> files = new TestFileSystem("", false);
	files->SetFileWithContents("Mock/C96/Flat.xdb", "<CacheResourceB13><value>11</value><children>"
		"<Item href=\"/Mock/C96/Flat.xdb\" id=\"leaf\"><CacheResourceB13><value>7</value></CacheResourceB13></Item>"
		"</children></CacheResourceB13>");
	files->SetFileWithContents("Mock/C96/A.xdb", "<CacheResourceB13><value>11</value><children>"
		"<Item href=\"/Mock/C96/A.xdb\" id=\"child\"><CacheResourceB13><value>7</value>"
		"<link href=\"/Mock/C96/B.xdb\"/></CacheResourceB13></Item></children></CacheResourceB13>");
	files->SetFileWithContents("Mock/C96/B.xdb", "<CacheResourceB13><value>13</value>"
		"<link href=\"/Mock/C96/A.xdb\"/></CacheResourceB13>");
	files->SetFileWithContents("Mock/C96/Shared.xdb", "<CacheResourceB13><value>17</value>"
		"<link href=\"/Mock/C96/Leaf.xdb\"/></CacheResourceB13>");
	files->SetFileWithContents("Mock/C96/Leaf.xdb", "<CacheResourceB13><value>19</value></CacheResourceB13>");
	files->SetFileWithContents("Mock/C96/Chain.xdb", "<CacheResourceB13>"
		"<link href=\"/Mock/C96/Shared.xdb\"/></CacheResourceB13>");
	files->SetFileWithContents("Mock/C96/Diamond.xdb", "<CacheResourceB13><children>"
		"<Item href=\"/Mock/C96/Chain.xdb\"/><Item href=\"/Mock/C96/Shared.xdb\"/>"
		"<Item href=\"/Mock/C96/Shared.xdb\"/></children></CacheResourceB13>");
	{
		CObj<NDb::DbResourceCache> cache = new NDb::DbResourceCache(files); NDb::SetResourceCache(cache);
		const auto graph = cache->PrecacheForce(NDb::DBID("/Mock/C96/Diamond.xdb"), 3);
		const auto* root = dynamic_cast<const CacheResourceB13*>(graph.GetPtr());
		check(root && root->children.size() == 3, "B13 forced diamond graph retains all authored reference slots");
		if (root && root->children.size() == 3 && root->children[0] && root->children[1] && root->children[2])
		{
			const auto* shared = root->children[1].GetPtr();
			check(shared == root->children[2].GetPtr() && shared == root->children[0]->link.GetPtr(),
				"B13 all repeated graph edges retain a single resource identity");
			check(shared->reads == 2, "B13 forced traversal rereads only for a greater remaining depth, not repeated equal-depth edges");
			check(shared->link.GetRawResourcePtr() && !shared->link.GetRawResourcePtr()->NeedReload(),
				"B13 a later shorter path expands descendants beyond the earlier shallow depth budget");
			check(shared->link && shared->link->value == 19 && shared->link->reads == 1,
				"B13 expanded descendant is fully read exactly once");
		}
		NDb::SetResourceCache(nullptr);
	}
	for (bool force : {false, true}) for (int warm : {0, 1, 2})
	{
		CObj<NDb::DbResourceCache> cache = new NDb::DbResourceCache(files);
		NDb::SetResourceCache(cache);
		{
			const auto leaf = NDb::Get<CacheResourceB13>(NDb::DBID("/Mock/C96/Flat.xdb", "leaf"));
			check(leaf && leaf->value == 7, "B13 cold inline lookup owns its result after the temporary root is released");
			const auto again = NDb::Get<CacheResourceB13>(NDb::DBID("/Mock/C96/Flat.xdb", "leaf"));
			check(leaf && again.GetPtr() == leaf.GetPtr(), "B13 retained inline identity survives repeated lookup without a retained root");
			NDb::Ptr<CacheResourceB13> original;
			NDb::Ptr<NDb::DbResource> shell;
			if (warm == 1) original = NDb::Get<CacheResourceB13>(NDb::DBID("/Mock/C96/A.xdb"));
			if (warm == 2) shell = cache->Get(NDb::DBID("/Mock/C96/A.xdb"), nullptr, false);
			const auto loaded = force ? cache->PrecacheForce(NDb::DBID("/Mock/C96/A.xdb"), 8)
				: cache->Precache(NDb::DBID("/Mock/C96/A.xdb"), 8);
			const auto* root = dynamic_cast<const CacheResourceB13*>(loaded.GetPtr());
			check(root && root->value == 11 && root->children.size() == 1,
				"B13 forced cyclic root retains its complete original XML fields");
			if (root && root->children.size() == 1 && root->children[0] && root->children[0]->link)
			{
				const auto* child = root->children[0].GetPtr();
				const auto* peer = child->link.GetPtr();
				check(child->value == 7 && peer->value == 13, "B13 forced cyclic graph fully reads inline and external fields");
				check(peer->link.GetPtr() == root, "B13 recursive lookup returns the already-published root identity");
				check(root->reentries == 0 && child->reentries == 0 && peer->reentries == 0,
					"B13 cache never reenters an active resource serializer");
				check(warm != 1 || original.GetPtr() == root, "B13 forced warm read preserves the held root identity");
				check(root->reads == (warm == 1 ? 2 : 1), "B13 root is serialized once per requested traversal, not once per cycle edge");
				check(!root->NeedReload(), "B13 precaching fully loads a previously shallow cached root");
				// Break the deliberately strong DB cycle before releasing test ownership.
				const_cast<CacheResourceB13*>(peer)->link = nullptr;
			}
			else check(false, "B13 cyclic fixture retains its actual child and peer links");
		}
		NDb::SetResourceCache(nullptr);
	}
	std::printf("B13 DB cache ownership checks=%d failed=%d\n", check.count - before, check.failed - failed);
}

/** Reproduce the original forced-precache failure without replacing any shipped resource. */
static void ResourceCacheAuthoredB13(Checks& check, const char* dataRoot)
{
	const int before = check.count, failed = check.failed;
	CObj<WinFileSystem> files = new WinFileSystem(dataRoot, false); RootFileSystem::RegisterFileSystem(files);
	NDb::SetResourceCache(NDb::CreateGameResourceCache(RootFileSystem::GetRootFileSystem(), &RootFileSystem::GetChangesProcessor()));
	{
		const auto talent = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A1.TALENT.xdb"));
		check(talent && talent->applicators.size() == 2, "B13 original A1 is held before forced global precaching");
		const auto globals = NDb::Get<NDb::UnitConstantsContainer>(NDb::DBID("/GameLogic/AILogic.AILP.xdb",
			"D05B858C-9B16-42A5-8BD1-62DB15F7E23E"));
		check(globals && !globals->vars.empty(), "B13 original inline global constants survive full forced AILogic traversal");
		const auto again = NDb::Get<NDb::Talent>(NDb::DBID("/Items/Talents/Class/Plane/Ability_A1.TALENT.xdb"));
		check(again.GetPtr() == talent.GetPtr() && talent && talent->applicators.size() == 2,
			"B13 forced original cyclic graph preserves the held A1 talent and completed applicators");
	}
	NDb::SetResourceCache(nullptr); RootFileSystem::UnregisterFileSystem(files);
	std::printf("B13 original cache graph checks=%d failed=%d\n", check.count - before, check.failed - failed);
}
