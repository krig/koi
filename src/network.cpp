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

#include <boost/uuid/uuid_generators.hpp>

#include "msg.hpp"
#include "sequence.hpp"
#include "network.hpp"
#include "nexus.hpp"
#include "clusterstate.hpp"

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace koi;

using boost::uuids::nil_uuid;


network::network(nexus& route, int netid)
	: _nexus(route), _netid(netid) {
}

void network::send(const uuid& ID, const string& name, int flags) {
	message m(ID, _netid, msg::base::HeartBeat);
	auto* hb = m.set_body<msg::heartbeat>();
	hb->_name = name;
	hb->_flags = flags;
	hb->_nodes.clear();
	hb->_elector = nil_uuid();
	hb->_master = nil_uuid();
	_nexus.send(m);
}

void network::send(const uuid& ID, const string& name, const clusterstate& s, int flags) {
	message m(ID, _netid, msg::base::HeartBeat);
	auto* hb = m.set_body<msg::heartbeat>();
	hb->_name = name;
	hb->_flags = flags;
	hb->_nodes.clear();
	hb->_nodes.reserve(s._nodes.size());
	copy(s._nodes.begin(), s._nodes.end(), back_inserter(hb->_nodes));
	hb->_elector = s._elector;
	hb->_master = s._master;
	_nexus.send(m);
}

void network::sendto(const uuid& to, const uuid& ID, const string& name, int flags) {
	net::endpoint who = whois(to);
	if (who == net::endpoint())
		return;

	message m(ID, _netid, msg::base::HeartBeat);
	auto* hb = m.set_body<msg::heartbeat>();
	hb->_name = name;
	hb->_flags = flags;
	hb->_nodes.clear();
	hb->_elector = nil_uuid();
	hb->_master = nil_uuid();
	_nexus.send(m, who);
}

void network::handle(message& m) {
	if (_sequence.check(m._sender_uuid, m._seqnr)) {
		_endpoints[m._sender_uuid] = m._from;
		_in_queue.push_back(m);
	}
}


message network::recv(const uuid& ID) {
	while (!_in_queue.empty()) {
		message m = _in_queue.front();
		_in_queue.pop_front();
		if (m._cluster_id != _netid || m._sender_uuid == ID)
			continue;

		// TODO: signal state change when this changes (?)
		return m;
	}
	return message(nil_uuid(), msg::base::HeartBeat);
}

net::endpoint network::whois(const uuid& id) const {
	if (id.is_nil()) {
		return net::endpoint();
	}
	else if (id == _nexus.cfg()._uuid) {
		return net::endpoint(net::ipaddr(), _nexus.cfg()._port);
	}
	else {
		endpointmap::const_iterator i = _endpoints.find(id);
		return (i != _endpoints.end()) ? i->second : net::endpoint();
	}
}
