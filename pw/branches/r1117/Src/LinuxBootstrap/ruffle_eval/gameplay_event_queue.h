#pragma once

#include "gameplay_events.h"
#include <stdexcept>
#include <utility>

/** Bounded exactly-once handoff between Flash rendering and native game commands.
 * Validate an entire batch before publishing any requests. Unsupported and
 * informational callbacks are counted but can never become executable requests.
 */
class PwRuffleGameplayEventQueue
{
public:
	/** Append one host reply atomically; overflow/malformed batches leave state intact. */
	void Append(std::string_view json)
	{
		auto decoded = PwRuffleDecodeGameplayEvents(json);
		auto next = pending_;
		std::size_t discarded = 0;
		for (auto& event : decoded)
		{
			using Kind = PwRuffleGameplayEvent::Kind;
			if (event.kind == Kind::Unsupported || event.kind == Kind::Informational)
			{
				++discarded;
				continue;
			}
			if (next.size() == PwRuffleGameplayEventLimits::MaxEvents)
				throw std::invalid_argument("Pending gameplay callback queue limit");
			next.push_back(std::move(event));
		}
		pending_.swap(next);
		received_ += decoded.size();
		discarded_ += discarded;
	}
	/** Transfer ownership once; the next drain is empty until another append. */
	std::vector<PwRuffleGameplayEvent> Drain()
	{
		std::vector<PwRuffleGameplayEvent> result;
		result.swap(pending_);
		return result;
	}
	/** Drop pending requests on focus loss, failed rendering or session shutdown. */
	void Clear() { pending_.clear(); }
	std::size_t Pending() const { return pending_.size(); }
	std::size_t Received() const { return received_; }
	std::size_t Discarded() const { return discarded_; }
private:
	std::vector<PwRuffleGameplayEvent> pending_;
	std::size_t received_ = 0, discarded_ = 0;
};
