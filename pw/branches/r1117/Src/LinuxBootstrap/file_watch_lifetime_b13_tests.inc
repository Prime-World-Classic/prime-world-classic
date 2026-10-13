#include "../System/FileSystem/FileSystem.h"
#include "../System/FileSystem/FileSystemChangesProcessor.h"

/** Owned storage and a destruction counter expose base-pointer deletion without ASan. */
struct FileWatchLifetimeCallbackB13 : IFileWatchFunctor
{
	int id;
	int& destroyed;
	std::vector<int>& calls;
	std::unique_ptr<int[]> storage;
	FileWatchLifetimeCallbackB13(int key, int& released, std::vector<int>& observed)
		: id(key), destroyed(released), calls(observed), storage(new int[8]{}) {}
	~FileWatchLifetimeCallbackB13() { ++destroyed; }
	void Call(const string& path) override {
		storage[0] = path.size();
		calls.push_back(id);
	}
	bool Equals(IFileWatchFunctor* other) override {
		const auto* callback = dynamic_cast<FileWatchLifetimeCallbackB13*>(other);
		return callback && callback->id == id;
	}
};

/** Feed deterministic file changes through the original processor, without OS watches. */
class FileWatchMockB13 : public IFileWatcher, public CObjectBase
{
	OBJECT_BASIC_METHODS(FileWatchMockB13);
public:
	TChangedFileList pending;
	string path;
	FileWatchMockB13() {}
	bool SomethingChanged(TChangedFileList* output) override {
		output->swap(pending);
		return !output->empty();
	}
	const string& GetPath() const override { return path; }
};

/** Cover both owned maps, explicit removal, extension rejection and real wrapper dispatch. */
static void FileWatchLifetimeB13(Checks& check)
{
	std::vector<int> calls;
	int destroyed = 0, keyDestroyed = 0;
	{
		IFileWatchFunctor* callback = new FileWatchLifetimeCallbackB13(7, destroyed, calls);
		callback->Call("test.xdb");
		delete callback;
	}
	check(destroyed == 1 && calls == std::vector<int>{7},
		"file watch lifetime: interface deletion destroys the concrete callback and storage");
	calls.clear(); destroyed = 0;
	{
		FileSystemChangesProcessor processor;
		CObj<FileWatchMockB13> watcher = new FileWatchMockB13;
		processor.SetFileWatcher(watcher);
		check(processor.RegisterFileWatchByExtension(".xdb", new FileWatchLifetimeCallbackB13(1, destroyed, calls)),
			"file watch lifetime: extension map accepts an owned callback");
		check(processor.RegisterFileWatchByName("UI/test.xdb", new FileWatchLifetimeCallbackB13(2, destroyed, calls)),
			"file watch lifetime: name map accepts an owned callback");
		watcher->pending.push_back("UI/test.xdb");
		processor.ProcessFileWatchers();
		check(calls == std::vector<int>({1, 2}) && destroyed == 0,
			"file watch lifetime: original extension-before-name dispatch preserves ownership");
		processor.ProcessFileWatchers();
		check(calls.size() == 2, "file watch lifetime: consumed mock changes are not dispatched twice");
	}
	check(destroyed == 2, "file watch lifetime: processor destruction releases both concrete map entries once");
	calls.clear(); destroyed = 0;
	{
		FileSystemChangesProcessor processor;
		check(processor.RegisterFileWatchByExtension(".xdb", new FileWatchLifetimeCallbackB13(3, destroyed, calls)),
			"file watch lifetime: extension callback is owned before removal");
		check(processor.RegisterFileWatchByName("UI/test.xdb", new FileWatchLifetimeCallbackB13(4, destroyed, calls)),
			"file watch lifetime: name callback is owned before removal");
		FileWatchLifetimeCallbackB13 extKey(3, keyDestroyed, calls), nameKey(4, keyDestroyed, calls);
		check(processor.UnregisterFileWatchByExtension(".xdb", &extKey) && destroyed == 1 && keyDestroyed == 0,
			"file watch lifetime: extension removal deletes the owned match, not the borrowed key");
		check(processor.UnregisterFileWatchByName("UI/test.xdb", &nameKey) && destroyed == 2 && keyDestroyed == 0,
			"file watch lifetime: name removal deletes the owned match, not the borrowed key");
	}
	check(destroyed == 2 && keyDestroyed == 2,
		"file watch lifetime: removed entries are not deleted twice and keys retain external ownership");
	calls.clear(); destroyed = 0;
	{
		FileSystemChangesProcessor processor;
		CObj<FileWatchMockB13> watcher = new FileWatchMockB13;
		processor.SetFileWatcher(watcher);
		check(processor.RegisterFileWatchByExtension(".xdb", new FileWatchLifetimeCallbackB13(5, destroyed, calls)),
			"file watch lifetime: original extension callback registers before duplicate");
		check(!processor.RegisterFileWatchByExtension(".xdb", new FileWatchLifetimeCallbackB13(5, destroyed, calls)) && destroyed == 1,
			"file watch lifetime: extension rejection destroys only the rejected concrete callback");
		watcher->pending.push_back("test.xdb");
		processor.ProcessFileWatchers();
		check(calls == std::vector<int>{5}, "file watch lifetime: original callback survives equal duplicate rejection");
	}
	check(destroyed == 2, "file watch lifetime: original and rejected extension callbacks each release once");
	{
		struct Receiver {
			string first, second;
			void First(const string& path) { first = path; }
			void Second(const string& path) { second = path; }
		} receiver;
		FileSystemChangesProcessor processor;
		CObj<FileWatchMockB13> watcher = new FileWatchMockB13;
		processor.SetFileWatcher(watcher);
		check(processor.RegisterFileWatchByExtension(".xdb", new FileWatchMethodFunctor<Receiver>(&receiver, &Receiver::First)) &&
			processor.RegisterFileWatchByName("UI/test.xdb", new FileWatchMethodFunctor<Receiver>(&receiver, &Receiver::Second)),
			"file watch lifetime: real member callbacks register independently");
		watcher->pending.push_back("UI/test.xdb");
		processor.ProcessFileWatchers();
		check(receiver.first == "UI/test.xdb" && receiver.second == "UI/test.xdb",
			"file watch lifetime: original member wrappers forward the exact file path");
		FileWatchMethodFunctor<Receiver> key(&receiver, &Receiver::First);
		check(processor.UnregisterFileWatchByExtension(".xdb", &key),
			"file watch lifetime: real member callback removal matches receiver and method");
		receiver.first.clear(); receiver.second.clear();
		watcher->pending.push_back("UI/test.xdb");
		processor.ProcessFileWatchers();
		check(receiver.first.empty() && receiver.second == "UI/test.xdb",
			"file watch lifetime: removed member no longer runs while the name callback remains live");
		// The real static wrapper also needs the same virtual deletion route.
		check(processor.RegisterFileWatchByExtension(".data", new FileWatchFunctor(+[](const string&) {})),
			"file watch lifetime: real static wrapper is owned through the same interface");
	}
}
