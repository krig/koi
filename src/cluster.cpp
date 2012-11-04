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
#include "masterstate.hpp"
#include "msg.hpp"
#include "sequence.hpp"
#include "clusterstate.hpp"
#include "network.hpp"
#include "cluster.hpp"

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace koi;

using boost::uuids::string_generator;
using boost::uuids::nil_uuid;
using namespace boost::posix_time;

namespace {
	void null_action(cluster*) {
	}
}

const char* koi::nodeflags_to_string(int flags) {
	const char* fval[] = {
		"....", "...E",
		"..R.", "..RE",
		".L..", ".L.E",
		".LR.", ".LRE",
		"F...", "F..E",
		"F.R.", "F.RE",
		"FL..", "FL.E",
		"FLR.", "FLRE"
	};
	return fval[flags];
}

cluster::cluster(nexus& route, const settings& cfg)
	: _state(),
	  _network(route, cfg._cluster_id),
	  _on_up(),
	  _on_down(),
	  _on_state_change(),
	  _cfg(cfg),
	  _leader(nil_uuid()),
	  _t(0),
	  _last_seen(0),
	  _candidate_time(0),
	  _flags(cfg._runner ? NodeFlag_Runner : 0),
	  _mode(Servant) {
	clear_callbacks();
}

cluster::~cluster() {
	if (_mode == Leader)
		_on_down(this);
	clear_callbacks();
}

void cluster::clear_callbacks() {
	_on_up = null_action;
	_on_down = null_action;
	_on_state_change = null_action;
}

bool cluster::settings_changed(const settings& newcfg, const settings& oldcfg) {
	if (newcfg._transport != oldcfg._transport) {
		LOG_INFO("Transport has changed: \"%s\" -> \"%s\"",
		         oldcfg._transport.c_str(), newcfg._transport.c_str());
		return false;
	}

	_network._netid = newcfg._cluster_id;

	return true;
}

void cluster::handle(message& m) {
	_network.handle(m);
}

void cluster::update() {
	++_t;

	_update_state();

	switch (_mode) {
	case Leader: update_leader(); break;
	case Candidate: update_candidate(); break;
	case Servant: update_servant(); break;
	default: throw std::runtime_error("Invalid cluster mode.");
	}

	_state.update_seen(_cfg._uuid);
	_state.timeout_nodes();
}

namespace {
	const uuid& msg_id(const message& m) {
		return m._sender_uuid;
	}

	bool msg_ok(const message& m) {
		return !msg_id(m).is_nil();
	}

	const net::endpoint& msg_addr(const message& m) {
		return m._from;
	}

	const char* msg_name(const message& m) {
		return m.body<msg::heartbeat>()->_name.c_str();
	}

	int msg_flags(const message& m) {
		return m.body<msg::heartbeat>()->_flags;
	}

	bool msg_from_leader(const message& m) {
		return msg_ok(m) && (msg_flags(m) & NodeFlag_Leader);
	}

}

void cluster::update_leader() {
	message m = _network.recv(_cfg._uuid);
	while (msg_ok(m)) {
		if (msg_from_leader(m)) {
			if (msg_id(m) > _cfg._uuid) {
				LOG_INFO("Cluster leadership lost.");
				_mode = Servant;
				set_leader(m);
				_on_down(this);
			}
		}

		bool changed = _state.update(msg_id(m),
		                             msg_name(m),
		                             msg_flags(m),
		                             msg_addr(m));
		if (changed)
			_on_state_change(this);

		m = _network.recv(_cfg._uuid);
	}
	if (_mode == Leader)
		_network.send(_cfg._uuid, _cfg._name, _state, _flags | NodeFlag_Leader);
}

void cluster::update_candidate() {
	message m = _network.recv(_cfg._uuid);
	while (msg_ok(m)) {
		if (msg_from_leader(m) && (msg_id(m) > _cfg._uuid)) {
			LOG_INFO("Defeated in leader election.");
			_mode = Servant;
			update_state(m);
			set_leader(m);
		}
		m = _network.recv(_cfg._uuid);
	}
	if (_mode != Servant) {
		_network.send(_cfg._uuid, _cfg._name, _flags | NodeFlag_Leader);

		if (_t - _candidate_time >= Limit) {
			_mode = Leader;

			message m2(_cfg._uuid);
			m2._from = net::endpoint(net::ipaddr(), _cfg._port);
			auto hb = m2.set_body<msg::heartbeat>();
			hb->_clusterid = _cfg._cluster_id;
			hb->_name = _cfg._name;
			hb->_flags = _flags;
			hb->_nodes = _state._nodes;
			hb->_elector = _state._elector;
			hb->_master = _state._master;
			set_leader(m2);
			LOG_INFO("Leader: t=%d, last_seen=%d, candidate_time=%d",
			         (int)_t, (int)_last_seen, (int)_candidate_time);
			_on_up(this);
		}
	}
}

void cluster::update_servant() {
	message m = _network.recv(_cfg._uuid);
	while (msg_ok(m)) {
		if (msg_from_leader(m) && (msg_id(m) != _cfg._uuid)) {
			_last_seen = _t;
			update_state(m);
			set_leader(m);
			reply_leader();
		}
		m = _network.recv(_cfg._uuid);
	}

	if (_cfg._elector && (_t - _last_seen >= Limit)) {
		LOG_INFO("Initiate leader election: %lld - %lld > %d",
		         (long long int)_t, (long long int)_last_seen, (int)Limit);
		_mode = Candidate;
		_candidate_time = _t;
		set_leader(message(nil_uuid(), msg::base::HeartBeat));
	}
}

void cluster::set_leader(const message& m) {
	if (msg_id(m) != _cfg._uuid)
		clear(NodeFlag_Leader);
	else if (!_cfg._uuid.is_nil())
		set(NodeFlag_Leader);

	if (_leader == msg_id(m))
		return;

	_leader = msg_id(m);

	if (_leader.is_nil())
		return;

	if (_leader != _cfg._uuid) {
		if (!_state.update(_leader, msg_name(m), msg_flags(m), msg_addr(m)))
			return;
	}
	else {
		if (!_state.update(_leader, _cfg._name, _flags, msg_addr(m)))
			return;
	}

	LOG_INFO("Leader is now: %s", lexical_cast<string>(_leader).c_str());
	_on_state_change(this);
}

void cluster::update_state(const message& m) {
	const auto hb = m.body<msg::heartbeat>();
	if (!hb->_elector.is_nil()) {
		bool changed = false;
		if (_state._elector != hb->_elector) {
			_state._elector = hb->_elector;
			LOG_INFO("ELECTOR: %s", lexical_cast<string>(_state._elector).c_str());
			changed = true;
		}
		if (_state._master != hb->_master) {
			_state._master = hb->_master;
			LOG_INFO("MASTER: %s", lexical_cast<string>(_state._master).c_str());
			changed = true;
		}
		FOREACH(const msg::heartbeat::node& n, hb->_nodes) {
			net::endpoint ep = n._addrs.get();
			if (n._id == m._sender_uuid)
				ep = m._from;
			changed |= _state.update(n._id,
			                         n._name,
			                         n._flags,
			                         ep);
		}
		if (changed) {
			_on_state_change(this);
		}
	}
}

void cluster::set(NodeFlags flag) {
	_flags = _flags | (int)flag;
	_update_state();
}

void cluster::clear(NodeFlags flag) {
	_flags = _flags & ~(int)flag;
	_update_state();
}

void cluster::reply_leader() {
	if (!_leader.is_nil()) {
		_network.sendto(_leader, _cfg._uuid, _cfg._name, _flags);
	}
}

// TODO: is _addr correct?
net::endpoint cluster::get_elector() const {
	if (_flags & NodeFlag_Elector) {
		return net::endpoint(net::ipaddr(), _cfg._port);
	}
	FOREACH(const clusterstate::node& n, _state._nodes) {
		if (n._flags & NodeFlag_Elector) {
			return _network.whois(n._id);
		}
	}
	return net::endpoint();
}

void cluster::_update_state() {
	net::endpoint ep = net::endpoint(net::ipaddr(), _cfg._port);
	bool changed = _state.update(_cfg._uuid,
	                             _cfg._name,
	                             _flags,
	                             ep);
	if (changed) {
		_on_state_change(this);
	}
}

masterstate cluster::get_masterstate() const {
	if (_state._master.is_nil())
		return masterstate();
	FOREACH(const clusterstate::node& n, _state._nodes) {
		if (_state._master == n._id) {
			masterstate ms;
			ms._last_seen = n._last_seen;
			ms._uuid = n._id;
			ms._name = n._name;
			ms._addr = n._addrs.get();
			return ms;
		}
	}
	return masterstate();
}

void cluster::set_masterstate(const masterstate& ms) {
	if (_state._elector != ms._elector ||
	    _state._master != ms._uuid) {
		_state._elector = ms._elector;
		_state._master = ms._uuid;

		_on_state_change(this);
	}
}

namespace koi {
	void force_reload_config();
}

bool cluster::rpc_handle(msg::request* rq, msg::response::values& data) {
	data["cmd"] = rq->_cmd;
	if (rq->_cmd == "reconfigure") {
		data["msg"] = "Reconfiguring node.";
		LOG_TRACE("Manual reconfigure triggered.");
		force_reload_config();
		return true;
	}
	return false;
}
