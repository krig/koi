/*
  Copyright (c) 2012 by Procera Networks, Inc. ("PROCERA")

  Permission to use, copy, modify, and/or distribute this software for
  any purpose with or without fee is hereby granted, provided that the
  above copyright notice and this permission notice appear in all
  copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND PROCERA DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL PROCERA BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
  OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
#include "koi.hpp"
#include "msg.hpp"
#include "sequence.hpp"
#include "network.hpp"
#include "masterstate.hpp"
#include "clusterstate.hpp"
#include "cluster.hpp"
#include <boost/uuid/uuid_generators.hpp>
#include "strfmt.hpp"

using namespace koi;
using namespace std;
using namespace boost;

using boost::uuids::string_generator;
using boost::uuids::nil_uuid;
using namespace boost::posix_time;

namespace {
	string to_string(const mru<net::endpoint>& mru) {
		return join(mru.begin(), mru.end(), ";");
	}

	void log_changes(const clusterstate::node& n, const clusterstate::node& old) {
		const char* n_name = n._name.c_str();
		if (old._flags != n._flags)
			LOG_INFO("%s flags changed: %s was %s", n_name,
			         koi::nodeflags_to_string(n._flags),
			         koi::nodeflags_to_string(old._flags));
		if (old._addrs != n._addrs)
			LOG_INFO("%s addrs changed: '%s' was '%s'", n_name, to_string(n._addrs).c_str(), to_string(old._addrs).c_str());
		if (old._name != n._name)
			LOG_INFO("%s name changed: was '%s'", n_name, old._name.c_str());
	}

	struct is_outdated {
		is_outdated() : _now(microsec_clock::local_time()), _timeout(seconds(5)) {
		}
		bool operator()(const clusterstate::node& n) {
			const bool od = _now - n._last_seen > _timeout;
			if (od) {
				LOG_INFO("State node timed out: %s",
				         lexical_cast<string>(n._id).c_str());
			}
			return od;
		}
		ptime _now;
		time_duration _timeout;
	};
}


clusterstate::clusterstate() {
	_elector = nil_uuid();
	_master = nil_uuid();
}

bool clusterstate::update(const uuid& ID,
                          const string& name,
                          int flags,
                          const net::endpoint& addr) {
	if (ID.is_nil())
		return false;

	node n(ID, name, flags, addr);
	bool found = false;
	bool changed = false;

	FOREACH(node& old, _nodes) {
		if (old._id != ID)
			continue;
		n._addrs.merge(old._addrs);
		changed = (old != n);
		if (changed &&
		    old._name.size() > n._name.size() &&
		    n._name.size() == 0) {
			// TODO: Make sure this is correct.
			// Special case: a higher-information
			// node update is being replaced with
			// a lower-information update
			changed = false;
			found = true;
			continue;
		}
		log_changes(n, old);
		old = n;
		found = true;
		break;
	}

	if (!found) {
		_nodes.push_back(n);
		changed = true;
	}

	if (n._flags & NodeFlag_Elector && _elector != n._id) {
		_elector = n._id;
		changed = true;
	}

	// TODO: this may not be the best way to handle this
	update_seen(ID);
	return changed;
}

void clusterstate::timeout_nodes() {
	auto new_end = remove_if(_nodes.begin(), _nodes.end(), is_outdated());
	_nodes.erase(new_end, _nodes.end());
}

void clusterstate::update_seen(const uuid& ID) {
	const ptime now = microsec_clock::local_time();
	FOREACH(node& n, _nodes) {
		if (n._id == ID) {
			n._last_seen = now;
			break;
		}
	}
}

#include <ostream>

namespace {
	struct abbrev
	{
		const uuid& id;
		explicit abbrev(const uuid& id_) : id(id_) {
		}
		unsigned int operator[](int idx) const { return id.data[idx]; }
	};

	template <typename ch, typename char_traits> inline
	ostream& operator<<(basic_ostream<ch, char_traits> &os, const abbrev& a) {
		const typename basic_ostream<ch, char_traits>::sentry ok(os);
		if (ok) {
			strfmt<16> s("%02x%02x%02x%02x", a[0], a[1], a[2], a[3]);
			os << s.c_str();
		}
		return os;
	}
}

string clusterstate::to_string() const {
	stringstream ss;
	bool first = true;
	bool found_elector = false;
	bool found_master = false;
	FOREACH(const node& n, _nodes) {
		if (first)
			first = false;
		else
			ss << ", ";
		ss << "(" << abbrev(n._id) << "/"
		   << n._name << " "
		   << nodeflags_to_string(n._flags) << " "
		   << n._addrs;

		if (_elector == n._id) {
			ss << " #elector";
			found_elector = true;
		}
		if (_master == n._id) {
			ss << " #master";
			found_master = true;
		}
		ss << ")";
	}
	if (!found_elector && !_elector.is_nil()) {
		ss << " elector: " << abbrev(_elector);
	}
	if (!found_master && !_master.is_nil()) {
		if (_master == _elector)
			ss << " master: elector";
		else
			ss << " master: " << abbrev(_master);
	}
	return ss.str();
}
