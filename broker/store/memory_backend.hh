#ifndef BROKER_STORE_MEMORY_BACKEND_HH
#define BROKER_STORE_MEMORY_BACKEND_HH

#include <broker/store/backend.hh>
#include <memory>

namespace broker { namespace store {

/**
 * An in-memory implementation of a storage backend.
 */
class memory_backend : public backend {
public:

	/**
	 * Construct the in-memory storage from a data store snapshot.
	 */
	memory_backend();

	/**
	 * Destructor.
	 */
	~memory_backend();

	/**
	 * Construct in-memory backend from a copy of another.
	 */
	memory_backend(memory_backend&);

	/**
	 * Construct in-memory backend by stealing another.
	 */
	memory_backend(memory_backend&&);

	/**
	 * Assign a replacement in-memory backend.
	 */
	memory_backend& operator=(memory_backend);

private:

	void do_increase_sequence() override;

	std::string do_last_error() const override;

	bool do_init(snapshot sss) override;

	const sequence_num& do_sequence() const override;

	bool do_insert(data k, data v, util::optional<expiration_time> t) override;

	bool do_increment(const data& k, int64_t by) override;

	bool do_add_to_set(const data& k, data element) override;

	bool do_remove_from_set(const data& k, const data& element) override;

	bool do_erase(const data& k) override;

	bool do_clear() override;

	util::optional<util::optional<data>> do_lookup(const data& k) const override;

	util::optional<bool> do_exists(const data& k) const override;

	util::optional<std::unordered_set<data>> do_keys() const override;

	util::optional<uint64_t> do_size() const override;

	util::optional<snapshot> do_snap() const override;

	util::optional<std::deque<expirable>> do_expiries() const override;

private:

	class impl;
	std::unique_ptr<impl> pimpl;
};

} // namespace store
} // namespace broker

#endif // BROKER_STORE_MEMORY_BACKEND_HH