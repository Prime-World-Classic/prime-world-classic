#include "../System/Notifier.h"

/** A concrete callback owns storage, making lost derived destruction observable without sanitizers. */
struct NotifierLifetimeCallbackB13 : INotifyCallback<int>
{
	int id;
	bool owned;
	int& destroyed;
	std::vector<int>& calls;
	std::unique_ptr<int[]> storage;
	NotifierLifetimeCallbackB13(int key, bool byNotifier, int& released, std::vector<int>& observed)
		: id(key), owned(byNotifier), destroyed(released), calls(observed), storage(new int[8]{}) {}
	~NotifierLifetimeCallbackB13() { ++destroyed; }
	void Call(const int& value) override { storage[0] = value; calls.push_back(id * 100 + value); }
	bool Equals(INotifyCallback<int>* other) override {
		const auto* callback = dynamic_cast<NotifierLifetimeCallbackB13*>(other);
		return callback && callback->id == id;
	}
	bool OwnerByNotifier() const override { return owned; }
};

/** Exercise every existing deletion site and verify that borrowed callbacks retain their owners. */
static void NotifierLifetimeB13(Checks& check)
{
	std::vector<int> calls;
	int destroyed = 0;
	{
		INotifyCallback<int>* callback = new NotifierLifetimeCallbackB13(1, false, destroyed, calls);
		callback->Call(7);
		delete callback;
	}
	check(destroyed == 1 && calls == std::vector<int>{107},
		"notifier lifetime: deleting through the interface destroys the concrete callback and owned storage");
	calls.clear(); destroyed = 0;
	{
		Notifier<int> notifier;
		check(notifier.Subscribe_(new NotifierLifetimeCallbackB13(1, true, destroyed, calls)),
			"notifier lifetime: first owned callback subscribes");
		check(notifier.Subscribe_(new NotifierLifetimeCallbackB13(2, true, destroyed, calls)),
			"notifier lifetime: second owned callback subscribes");
		notifier.Notify(5);
		check(calls == std::vector<int>({105, 205}) && destroyed == 0,
			"notifier lifetime: notification retains insertion order and does not delete callbacks");
	}
	check(destroyed == 2, "notifier lifetime: notifier destruction releases each owned concrete callback once");
	calls.clear(); destroyed = 0;
	{
		Notifier<int> notifier;
		auto* accepted = new NotifierLifetimeCallbackB13(3, true, destroyed, calls);
		check(notifier.Subscribe_(accepted), "notifier lifetime: owned callback is admitted before duplicate test");
		check(!notifier.Subscribe_(new NotifierLifetimeCallbackB13(3, true, destroyed, calls)) && destroyed == 1,
			"notifier lifetime: rejecting an owned duplicate destroys only the rejected concrete callback");
		notifier.Notify(9);
		check(calls == std::vector<int>{309}, "notifier lifetime: original callback survives duplicate rejection");
		check(notifier.Unsubscribe_(accepted) && notifier.Empty() && destroyed == 2,
			"notifier lifetime: explicit unsubscribe destroys the owned callback once");
	}
	check(destroyed == 2, "notifier lifetime: notifier destruction does not repeat an unsubscribe deletion");
	calls.clear(); destroyed = 0;
	{
		NotifierLifetimeCallbackB13 borrowed(4, false, destroyed, calls);
		{
			Notifier<int> notifier;
			check(notifier.Subscribe_(&borrowed), "notifier lifetime: borrowed callback subscribes");
			check(notifier.Unsubscribe_(&borrowed) && destroyed == 0,
				"notifier lifetime: unsubscribing a borrowed callback leaves its owner intact");
			check(notifier.Subscribe_(&borrowed), "notifier lifetime: borrowed callback can subscribe again");
			notifier.Notify(4);
		}
		check(destroyed == 0 && calls == std::vector<int>{404},
			"notifier lifetime: destroying a notifier never deletes a borrowed callback");
		borrowed.Call(8);
	}
	check(destroyed == 1 && calls == std::vector<int>({404, 408}),
		"notifier lifetime: the external owner destroys its usable borrowed callback once");
	calls.clear(); destroyed = 0;
	{
		Notifier<int> notifier;
		NotifierLifetimeCallbackB13 original(5, false, destroyed, calls);
		NotifierLifetimeCallbackB13 duplicate(5, false, destroyed, calls);
		check(notifier.Subscribe_(&original) && !notifier.Subscribe_(&duplicate) && destroyed == 0,
			"notifier lifetime: rejecting a borrowed duplicate deletes neither external object");
		check(notifier.Unsubscribe_(&duplicate) && notifier.Empty() && destroyed == 0,
			"notifier lifetime: equal borrowed key unsubscribes without destroying either owner");
	}
	check(destroyed == 2, "notifier lifetime: borrowed duplicate objects are each externally destroyed");
	struct Receiver {
		int first = 0, second = 0;
		void First(const int& value) { first += value; }
		void Second(const int& value) { second += value; }
		void Resource(const NDb::DBID&) {}
	} receiver;
	{
		Notifier<int> notifier;
		check(notifier.Subscribe(&receiver, &Receiver::First) && notifier.Subscribe(&receiver, &Receiver::Second),
			"notifier lifetime: real member-method callbacks remain independently subscribed");
		notifier.Notify(2);
		check(notifier.Unsubscribe(&receiver, &Receiver::First),
			"notifier lifetime: member-method callback matches its original receiver and handler");
		notifier.Notify(3);
		check(receiver.first == 2 && receiver.second == 5,
			"notifier lifetime: unsubscribing one method preserves the other method");
	}
	{
		NotifiersRegistry<int, int> registry;
		check(registry.Subscribe(7, &receiver, &Receiver::First), "notifier lifetime: registry owns method callbacks");
		registry.Notify(7, 4);
		check(registry.Unsubscribe(7, &receiver, &Receiver::First),
			"notifier lifetime: registry removes the callback and its empty notifier");
		registry.Notify(7, 8);
		check(receiver.first == 6, "notifier lifetime: erased registry callback receives no later notification");
	}
	for (int iteration = 0; iteration != 8; ++iteration) {
		NDb::ChangeablePtr<NDb::AILogicParameters> resource(&receiver, &Receiver::Resource);
		check(resource.IsEmpty(), "notifier lifetime: real ChangeablePtr owns and destroys its null-resource callback");
	}
}
