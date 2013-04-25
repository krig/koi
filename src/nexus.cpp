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
#include "nexus.hpp"
#include "masterstate.hpp"
#include "elector.hpp"
#include "runner.hpp"
#include "network.hpp"
#include "clusterstate.hpp"
#include "cluster.hpp"
#include "settings.hpp"

#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>

using namespace std;
using namespace boost;
using namespace boost::posix_time;
using namespace boost::uuids;
using namespace boost::asio;

namespace koi {
	namespace {
		typedef list<message> messagequeue;
		typedef vector<net::endpoint> linklist;
	}

	struct nexus_impl {
		nexus_impl(nexus& route, net::io_service& io, const settings& conf)
			: _io(io),
			  _sock(io),
			  _cfg(conf),
			  _cluster(route, _cfg) {
		}

		void init_socket(const net::endpoint& listen);
		void handle_receive_from(const error_code& err, size_t nbytes);
		bool parse_message(message& to);
		bool settings_changed(const settings& newcfg, const settings& oldcfg);
		void rpc_response(const net::endpoint& to,
		                  const msg::response::values& data);
		void update();

		net::io_service& _io;
		net::socket _sock;
		settings _cfg;
		cluster _cluster;
		boost::shared_ptr<runner> _runner;
		boost::shared_ptr<elector> _elector;
		net::endpoint _in_remote;
		vector<uint8_t> _in_buffer;
		vector<uint8_t> _out_buffer;
		linklist _links;
		messagequeue _in_queue;
	};

	nexus::nexus(net::io_service& ioservice, const settings& conf)
		: _impl(new nexus_impl(*this, ioservice, conf)) {
		_redirecting_rpc.insert("start");
		_redirecting_rpc.insert("stop");
		_redirecting_rpc.insert("recover");
		_redirecting_rpc.insert("reconfigure");

		_elector_rpc["status"] = elector_rpcfn(&elector::rpc_status);
		_elector_rpc["tree"] = _elector_rpc["status"];
		_elector_rpc["promote"] = elector_rpcfn(&elector::rpc_promote);
		_elector_rpc["demote"] = elector_rpcfn(&elector::rpc_demote);
		_elector_rpc["elect"] = elector_rpcfn(&elector::rpc_elect);
		_elector_rpc["failures"] = elector_rpcfn(&elector::rpc_failures);
		_elector_rpc["maintenance"] = elector_rpcfn(&elector::rpc_maintenance);
		_runner_rpc["start"] = runner_rpcfn(&runner::rpc_start);
		_runner_rpc["stop"] = runner_rpcfn(&runner::rpc_stop);

		_impl->init_socket(net::endpoint(net::ipaddr(), conf._port));
	}

	nexus::~nexus() {
		if (_impl) {
			_impl->_cluster.clear_callbacks();
		}
	}

	void nexus_impl::init_socket(const net::endpoint& listen) {
		const uint16_t max_inc = 1000;
		uint16_t inc = 0;
		net::endpoint actual(listen);
		for (;;) {
			boost::system::error_code ec;
			_sock.open(actual.protocol(), ec);
			if (!ec) {
				if (_cfg._reuse_address) {
					_sock.set_option(asio::ip::udp::socket::reuse_address(true));
				}
				_sock.bind(actual, ec);
			}
			if (ec) {
				if (inc < max_inc && _cfg._incremental_port) {
					LOG_INFO("Address %s already in use; trying to increment port by 1",
					         to_string(actual).c_str());
					actual.port(listen.port() + (++inc));
					if (_sock.is_open())
						_sock.close(ec);
					continue;
				}
				else {
					LOG_INFO("Address %s already in use.", to_string(actual).c_str());
					throw runtime_error("Address already in use.");
				}
			}
			break;
		}

		_cfg._port = actual.port();


		_in_buffer.resize(msg::MAX_MSG_LEN);
		_sock.async_receive_from(asio::buffer(_in_buffer), _in_remote,
		                         bind(&nexus_impl::handle_receive_from, this,
		                              asio::placeholders::error,
		                              asio::placeholders::bytes_transferred));
	}

	void nexus_impl::handle_receive_from(const error_code& err, size_t nbytes) {
		if (!err) {
			_in_buffer.resize(nbytes);

			message m;
			if (parse_message(m) && m._cluster_id == _cfg._cluster_id) {
				_in_queue.push_back(m);
			}
		}

		_in_buffer.resize(msg::MAX_MSG_LEN);
		_sock.async_receive_from(asio::buffer(_in_buffer), _in_remote,
		                         bind(&nexus_impl::handle_receive_from, this,
		                              asio::placeholders::error,
		                              asio::placeholders::bytes_transferred));
	}

	bool nexus_impl::parse_message(message& to) {
		to._from = _in_remote;
		return msg::decode(&to, _in_buffer, _cfg._pass);
	}

	bool nexus_impl::settings_changed(const settings& newcfg, const settings& oldcfg) {
		if (newcfg._uuid != oldcfg._uuid) {
			LOG_TRACE("UUID change forces restart.");
			return false;
		}

		if (_sock.local_endpoint() != net::endpoint(net::ipaddr(), newcfg._port)) {
			string e0 = lexical_cast<string>(_sock.local_endpoint());
			string e1 = lexical_cast<string>(net::endpoint(net::ipaddr(), newcfg._port));
			LOG_TRACE("Local endpoint has changed: %s -> %s.", e0.c_str(), e1.c_str());
			return false;
		}

		_cfg = newcfg;

		if (!_cluster.settings_changed(newcfg, oldcfg))
			return false;

		if (_runner && !_runner->settings_changed(newcfg, oldcfg))
			return false;

		if (_elector && !_elector->settings_changed(newcfg, oldcfg))
			return false;

		return true;
	}

	void nexus_impl::rpc_response(const net::endpoint& to,
	                              const msg::response::values& data) {
		message m(_cfg._uuid, _cfg._cluster_id);
		auto rs = m.set_body<msg::response>();
		rs->_response = data;

		_out_buffer.reserve(msg::MAX_MSG_LEN);
		if (msg::encode(_out_buffer, &m, _cfg._pass)) {
			error_code ec;
			_sock.send_to(asio::buffer(_out_buffer), to, 0, ec);
			if (ec) {
				LOG_ERROR("send_to error: %d %s", ec.value(), ec.message().c_str());
				// TODO: handle / recover
			}
		}
		else {
			LOG_ERROR("Failed to encode message when sending response to %s.",
			          to_string(to).c_str());
		}
	}

	const settings& nexus::cfg() const {
		return _impl->_cfg;
	}

	settings& nexus::cfg() {
		return _impl->_cfg;
	}

	net::io_service& nexus::io() const {
		return _impl->_io;
	}

	net::endpoint nexus::get_elector() const {
		return _impl->_cluster.get_elector();
	}

	masterstate nexus::get_masterstate() const {
		return _impl->_cluster.get_masterstate();
	}

	void nexus::set_masterstate(const masterstate& ms) {
		return _impl->_cluster.set_masterstate(ms);
	}

	bool nexus::has_quorum() const {
		if (_impl->_cfg._cluster_quorum < 1)
			return true;
		return (int)nodes().size() >= _impl->_cfg._cluster_quorum;
	}


	void nexus::add_link(const net::endpoint& remote) {
		using namespace boost::asio;
		remove_link(remote);
		_impl->_links.push_back(remote);

		if (net::is_multicast(remote.address())) {
			// TODO: outbound interface, IPV6 support..
			//if (_local.protocol() == net::endpoint::protocol_type::v4())
			//    _sock.set_option(ip::multicast::outbound_interface(_local.address().to_v4()));
			_impl->_sock.set_option(ip::multicast::enable_loopback(true));
			_impl->_sock.set_option(ip::multicast::join_group(remote.address()));

			LOG_INFO("Link: multicast %s", to_string(remote).c_str());
		}
		else {
			LOG_INFO("Link: unicast %s", to_string(remote).c_str());
		}
	}

	void nexus::remove_link(const net::endpoint& remote) {
		for (auto i = _impl->_links.begin(), e = _impl->_links.end(); i != e; ++i) {
			if (*i == remote) {
				_impl->_links.erase(i);
				break;
			}
		}
	}

	void nexus::init_links() {
		const uint16_t port = _impl->_cfg._port;
		const string& ts = _impl->_cfg._transport;
		// parse string...
		stringvec tsv = split(ts, ",; ");

		net::endpoint e0;
		for (size_t i = 0; i < tsv.size(); ++i) {
			e0 = parse_endpoint(tsv[i].c_str(), port);

			add_link(e0);
		}
	}


	void nexus::send(const message& m) {
		_impl->_out_buffer.reserve(msg::MAX_MSG_LEN);
		if (msg::encode(_impl->_out_buffer, &m, _impl->_cfg._pass)) {
			FOREACH(net::endpoint const& remote, _impl->_links) {
				if (!net::is_multicast(remote.address()) || m._op == msg::base::HeartBeat) {
					if (koi::debug_mode) {
						LOG_TRACE("%s: %d bytes to %s",
						          msg::type_to_string(m._op),
						          (int)_impl->_out_buffer.size(),
						          to_string(remote).c_str());
					}
					error_code ec;
					_impl->_sock.send_to(asio::buffer(_impl->_out_buffer), remote, 0, ec);
					if (ec) {
						LOG_ERROR("send_to error: %d %s", ec.value(), ec.message().c_str());
						// TODO: handle/recover
					}
				}
			}
		}
	}

	void nexus::send(const message& m, const net::endpoint& to) {
		_impl->_out_buffer.reserve(msg::MAX_MSG_LEN);
		if (msg::encode(_impl->_out_buffer, &m, _impl->_cfg._pass)) {
			if (koi::debug_mode) {
				LOG_TRACE("%s: %d bytes to %s",
				          msg::type_to_string(m._op),
				          (int)_impl->_out_buffer.size(),
				          to_string(to).c_str());
			}
			error_code ec;
			_impl->_sock.send_to(asio::buffer(_impl->_out_buffer), to, 0, ec);

			if (ec) {
				LOG_ERROR("send_to error: %d %s", ec.value(), ec.message().c_str());
				// TODO: handle/recover
			}
		}
	}

	pair<bool, net::endpoint> nexus::redirect_to(const string& nodename) {
		FOREACH(const node& n, nodes()) {
			if (nodename == n._name) {
				return make_pair(true, n._addrs.get());
			}
		}
		try {
			uuid uid = string_generator()(nodename);
			FOREACH(const node& n, nodes()) {
				if (uid == n._id) {
					return make_pair(true, n._addrs.get());
				}
			}
		}
		catch (runtime_error& e) {
		}
		return make_pair(false, net::endpoint());
	}

	void nexus::handle(message& m) {
		// TODO: handle RPC request/response
		if (m._op != msg::base::Request) {
			LOG_WARN("Ignoring message: %s, %s", msg::type_to_string(m._op), to_string(m._from).c_str());
			return;
		}
		auto rq = m.body<msg::request>();
		msg::response::values data;

		bool handled = false;

		if (_redirecting_rpc.count(rq->_cmd) && !rq->_args.empty()) {
			auto tgt = redirect_to(rq->_args.front());
			if (tgt.first) {
				data["redirect"] = to_string(tgt.second);
				data["cmd"] = rq->_cmd;
				data["args"] = vector<string>(++rq->_args.begin(), rq->_args.end());
			}
			else {
				LOG_WARN("Unknown target node: %s", rq->_args.front().c_str());
			}
			handled = true;
		}

		if (!handled) {
			handled = rpc_handle(rq, data) ||
				_impl->_cluster.rpc_handle(rq, data) ||
				rpc_elector(rq, data) ||
				rpc_runner(rq, data) ||
				rpc_recover(rq, data);
		}

		if (!handled) {
			LOG_WARN("Ignoring unknown RPC request: %s",
			         rq->_cmd.c_str());
			data["msg"] = "Error: Unknown request.";
		}
		else {
			LOG_INFO("RPC: %s, %s", rq->_cmd.c_str(), to_string(m._from).c_str());
		}

		_impl->rpc_response(m._from, data);
	}

	bool nexus::rpc_handle(msg::request* rq, msg::response::values& data) {
		if (rq->_cmd == "local") {
			data["uuid"] = _impl->_cfg._uuid;
			data["name"] = _impl->_cfg._name;
			data["port"] = _impl->_cfg._port;
			data["maintenance"] = _impl->_cfg._cluster_maintenance;
			data["runner"] = _impl->_cfg._runner;
			data["elector"] = _impl->_cfg._elector;
			data["starttime"] = _impl->_cfg._starttime;
			data["nodes"] = (int)nodes().size();
			data["cluster"] = _impl->_cluster._state.to_string();
			if (_impl->_runner) {
				data["state"] = _impl->_runner->_state;
			}
			else if (_impl->_elector) {
				data["state"] = S_Elector;
			}
			else {
				data["state"] = S_Other;
			}
			return true;
		}
		return false;
	}

	bool nexus::rpc_elector(msg::request* rq, msg::response::values& data) {
		elector_rpc_functions::iterator i;
		i = _elector_rpc.find(rq->_cmd);
		if (i == _elector_rpc.end())
			return false;

		if (_impl->_elector) {
			(i->second)(_impl->_elector.get(), rq, data);
		}
		else if (get_elector() != net::endpoint()) {
			data["redirect"] = to_string(get_elector());
			data["cmd"] = rq->_cmd;
			data["args"] = rq->_args;
		}
		else {
			data["msg"] = "No elector found. Cluster state: " + _impl->_cluster._state.to_string();
		}
		return true;
	}

	bool nexus::rpc_runner(msg::request* rq, msg::response::values& data) {
		runner_rpc_functions::iterator j;
		j = _runner_rpc.find(rq->_cmd);
		if (j == _runner_rpc.end())
			return false;

		if (_impl->_runner) {
			(j->second)(_impl->_runner.get(), rq, data);
		}
		else {
			data["msg"] = "Error: Not a runner.";
		}
		return true;
	}

	bool nexus::rpc_recover(msg::request* rq, msg::response::values& data) {
		if (rq->_cmd != "recover")
			return false;

		// find node that has failed, forward request to that node
		FOREACH(const node& n, nodes()) {
			if (n._flags & NodeFlag_Failed) {
				if ((n._id == _impl->_cfg._uuid) &&
				    _impl->_runner) {
					_impl->_runner->rpc_recover(rq, data);
					return true;
				}

				LOG_TRACE("recover -> %s", to_string(n._addrs.get()).c_str());
				data["redirect"] = to_string(n._addrs.get());
				data["cmd"] = rq->_cmd;
				data["args"] = vector<string>();
				return true;
			}
		}
		data["msg"] = "No failed nodes to recover.";
		return true;
	}

	void nexus::_route(message& m) {
		switch (m._op) {
		case msg::base::HealthReport:
			if (_impl->_elector)
				_impl->_elector->handle(m);
			break;
		case msg::base::StateUpdate:
			if (_impl->_runner)
				_impl->_runner->handle(m);
			break;
		case msg::base::HeartBeat:
			_impl->_cluster.handle(m);
			break;
		default:
			handle(m);
			break;
		}
	}

	bool nexus::init() {
		_impl->_cluster._on_up = bind(&nexus::up, this, _1);
		_impl->_cluster._on_down = bind(&nexus::down, this, _1);
		_impl->_cluster._on_state_change = bind(&nexus::state_change, this, _1);

		if (_impl->_cfg._runner) {
			auto r = boost::shared_ptr<runner>(new runner(*this));
			if (!r->init()) {
				LOG_ERROR("Runner initialization error");
				return false;
			}
			r->start();
			_impl->_runner = r;
		}

		_cluster_t = min_date_time;

		init_links();
		return true;
	}

	void nexus::up(cluster* c) {
		// cluster becomes leader: start elector service if not already started
		// TODO: possibly delay this until no
		// other node reports running an elector? (do it in state_change
		// or update() instead)
		if (!_impl->_elector) {
			auto a = boost::shared_ptr<elector>(new elector(*this));
			if (!a->init()) {
				LOG_ERROR("Failed to start elector");
				throw runtime_error("Failed to start elector on node. Terminating.");
			}
			_impl->_elector = a;

			// start elector
			a->start();
			LOG_INFO("Started elector");

			c->set(NodeFlag_Elector);
			LOG_INFO("Cluster leader up: started elector.");
		}
		else {
			LOG_INFO("Elector running when node became leader.");
		}

		state_change(c);
	}

	void nexus::down(cluster* c) {
		// cluster becomes slave: stop elector service if started
		if (_impl->_elector) {
			_impl->_elector->stop();
			_impl->_elector.reset();

			c->clear(NodeFlag_Elector);
		}
		LOG_INFO("Cluster leader down: stopped elector.");

		state_change(c);
	}

	void nexus::state_change(cluster* c) {
		string s = c->_state.to_string();
		LOG_TRACE("State: %s", s.c_str());
	}

	void nexus_impl::update() {
		if (_elector) {
			_elector->update();
		}

		if (_runner) {
			_runner->update();

			if (_runner->_state != S_Failed) {
				if (_cluster.flags() & NodeFlag_Failed) {
					_cluster.clear(NodeFlag_Failed);
				}
			}
			else {
				if (!(_cluster.flags() & NodeFlag_Failed)) {
					_cluster.set(NodeFlag_Failed);
				}
			}
		}
	}

	void nexus::update() {
		// re-route incoming messages to the appropriate queues
		// TODO: handle RPC requests/responses here
		messagequeue processing;
		processing.splice(processing.end(), _impl->_in_queue);
		FOREACH(message& m, processing)
			_route(m);

		ptime now(microsec_clock::universal_time());
		if (now - _cluster_t >= microseconds(_impl->_cfg._cluster_update_interval)) {
			_impl->_cluster.update();
			_cluster_t = now;
		}

		_impl->update();
	}

	bool nexus::settings_changed(const settings& newcfg) {
		settings old = _impl->_cfg;
		if (old._runner != newcfg._runner) {
			LOG_WARN("Runner %s", newcfg._runner ? "enabled" : "disabled");
			return false;
		}
		if (old._transport != newcfg._transport) {
			LOG_WARN("New transports: %s", newcfg._transport.c_str());
			return false;
		}
		return _impl->settings_changed(newcfg, old);
	}

	const nexus::nodelist& nexus::nodes() const {
		return _impl->_cluster._state._nodes;
	}

}
