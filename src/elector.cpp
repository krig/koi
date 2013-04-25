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
#include "elector.hpp"
#include "nexus.hpp"
#include "sequence.hpp"
#include "network.hpp"
#include "clusterstate.hpp"
#include "cluster.hpp"

#include <sstream>
#include <boost/bind.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <algorithm>

#include "strfmt.hpp"
#include "stringutil.hpp"
#include "file.hpp"
#include "sha1.hpp"
#include "archive.hpp"
#include "masterstate.hpp"

using namespace std;
using namespace boost;
using namespace boost::posix_time;
using namespace boost::uuids;

namespace {
	const size_t MAX_FAILURES = 10;

	struct dirtyflag {
		bool _dirty;
		dirtyflag(bool init) : _dirty(init) {
		}
		inline void set(bool b) {
			if (b)
				_dirty = true;
		}
		inline void operator=(bool b) {
			set(b);
		}
		inline bool get() const {
			return _dirty;
		}
	};
}

namespace koi {
	namespace {
		typedef elector::runners::iterator irunner;
		bool runner_electability(const irunner& a, const irunner& b) {
			if (a->second._uptime != b->second._uptime)
				return a->second._uptime > b->second._uptime;
			return a->second._last_seen < b->second._last_seen;
		}
	}

	elector::runner_info::runner_info()
		: _last_seen(min_date_time),
		  _last_failed(min_date_time),
		  _name(),
		  _uuid(nil_uuid()),
		  _uptime(0),
		  _state(S_Disconnected),
		  _mode(R_Passive),
		  _maintenance(false),
		  _service_action(Svc_Stop),
		  _endpoints(),
		  _services() {
	}

	bool elector::runner_info::alive(uint64_t master_dead_time, ptime const& now) const {
		return now - _last_seen <= microseconds(master_dead_time);
	}

	bool elector::runner_info::electable(const ptime& now, uint64_t promotion_timeout) const {
		if (_state <= S_Stopped)
			return false;

		if (_mode == R_Passive)
			return false;

		if (!(now - _last_failed > microseconds(promotion_timeout)))
			return false;

		return true;
	}


	bool elector::runner_info::promoted_service() const {
		FOREACH(const auto& s, _services)
			if (s._state >= Svc_Demoting)
				return true;
		return false;
	}

	bool elector::runner_info::failed_service() const {
		FOREACH(const auto& s, _services)
			if (s._failed)
				return true;
		return false;
	}

	void elector::runner_info::read(const masterstate& state) {
		_name = state._name;
		_last_seen = state._last_seen;
		_last_failed = ptime(min_date_time);
		_uptime = 0;
		_state = S_Master;
		_mode = R_Active;
	}

	string elector::failure_info::to_string() const {
		stringstream ss;
		ss << _time << ": " << _name << " (" << _uuid << ")";
		return ss.str();
	}

	elector::elector(nexus& route)
		: _emitter(route, route.cfg()._elector_tick_interval) {
		_emitter._on_tick = bind(&elector::on_tick, this, _1);
		_manual_master_mode = false;
		_master = _runners.end();
		_target_master = _runners.end();
		_failures.reserve(MAX_FAILURES);

		_state_sum = 0;
	}

	elector::~elector() {
		save_state();
	}

	bool elector::init() {
		_starttime = microsec_clock::universal_time();

		load_state();

		return true;
	}

	bool elector::settings_changed(const settings& newcfg, const settings&) {
		if (newcfg._elector == false) {
			LOG_INFO("Elector disabled forces restart");
			return false;
		}
		return true;
	}

	void elector::transition_runner(runners::iterator i, State newstate) {
		if (i->second._state != newstate) {
			string id = lexical_cast<string>(i->second._uuid);
			LOG_INFO("%s (%s): %s -> %s",
			         i->second._name.c_str(),
			         to_string(i->first).c_str(),
			         state_to_string(i->second._state),
			         state_to_string(newstate));
			i->second._state = newstate;

			if (newstate == S_Disconnected) {
				i->second._services.clear();
			}
			else if (newstate == S_Failed) {
				register_failure(i->second);
			}
		}
	}

	bool elector::_repromote_master() {
		bool dirty = false;
		if (_master == _runners.end() &&
		    _target_master == _runners.end() &&
		    _manual_master_mode == false) {
			for (auto i = _runners.begin(); i != _runners.end(); ++i) {
				if (i->second._state > S_Slave) {
					LOG_INFO("%s (%s) is promoted.",
					         i->second._name.c_str(), to_string(i->first).c_str());
					elect_node(i);
					dirty = true;
					break;
				}
			}
		}
		return dirty;
	}

	bool elector::_check_runner_health(const ptime& now, int& npromoted, int& nfailed) {
		bool dirty = false;
		for (auto i = _runners.begin(); i != _runners.end(); ++i) {
			if (i->second._state > S_Disconnected) {
				if (!i->second.alive(_emitter._nexus.cfg()._master_dead_time, now)) {
					if (_emitter.uptime(_starttime) > _emitter._nexus.cfg()._elector_initial_promotion_delay/units::milli) {
						LOG_INFO("%s (%s) not seen for %d seconds. Mark as offline.",
						         i->second._name.c_str(), to_string(i->first).c_str(),
						         (int)(_emitter._nexus.cfg()._master_dead_time/1e6));
						transition_runner(i, S_Disconnected);
						dirty = true;
					}
				}
				else {
					if (i->second.promoted_service() != 0)
						++npromoted;
					if (i->second.failed_service()) {
						++nfailed;
						LOG_TRACE("Found failed services on %s (%s), mark as failed",
						          i->second._name.c_str(), to_string(i->first).c_str());
						transition_runner(i, S_Failed);
						dirty = true;
					}
				}
			}
		}
		return dirty;
	}

	bool elector::_check_master_health() {
		if (_master != _runners.end()) {
			bool demote = false;

			if (_master->second._state <= S_Stopped) {
				LOG_TRACE("Master state <= Stopped. Master demoted.");
				demote = true;
			}

			if (_master->second._mode == R_Passive) {
				LOG_TRACE("Master is passive. Master demoted.");
				demote = true;
			}

			if (!_emitter._nexus.has_quorum()) {
				LOG_TRACE("Loss of quorum. Master demoted.");
				demote = true;
			}

			if (demote) {
				_master = _runners.end();
				return true;
			}
		}
		return false;
	}

	bool elector::_should_skip_election(int npromoted) {
		return (npromoted > 0 ||
		        _runners.empty() ||
		        _master != _runners.end() ||
		        _manual_master_mode ||
		        !_emitter._nexus.has_quorum() ||
		        _emitter.uptime(_starttime) < _emitter._nexus.cfg()._elector_initial_promotion_delay/units::milli);
	}

	bool elector::_elect_target_master() {
		if (elect_node(_target_master)) {
			LOG_INFO("Target master promoted.");
			return true;
		}
		else {
			LOG_WARN("Target master promotion failed. Electing alternative master.");
			_manual_master_mode = false;
			_target_master = _runners.end();
			return false;
		}
	}

	bool elector::_elect_candidate(runners::iterator runner) {
		if (elect_node(runner)) {
			LOG_INFO("Found eligible runner: %s (%s). Electing as master.",
			         runner->second._name.c_str(),
			         to_string(runner->first).c_str());
			return true;
		}
		return false;
	}

	bool elector::_find_candidates(ptime now, vector<runners::iterator>& candidates) {
		for (auto i = _runners.begin(); i != _runners.end(); ++i)
			if (i->second.electable(now, _emitter._nexus.cfg()._runner_failure_promotion_timeout))
				candidates.push_back(i);

		// TODO: Is this really desirable behavior?
		// With no other candidates available, ignore
		// the failure promotion timeout
		if (candidates.size() == 0) {
			for (auto i = _runners.begin(); i != _runners.end(); ++i)
				if (i->second.electable(now, 0))
					candidates.push_back(i);
		}

		return candidates.size() != 0;
	}

	// Election algorithm
	// returns: dirty flag
	bool elector::_election(ptime now, int npromoted) {
		if (_should_skip_election(npromoted))
			return false;

		if (_target_master != _runners.end())
			return _elect_target_master() != true;

		vector<runners::iterator> candidates;
		if (!_find_candidates(now, candidates))
			return false;

		std::sort(candidates.begin(), candidates.end(), runner_electability);

		auto winner = candidates.begin();

		// See if anyone is already a winner
		for (auto i = candidates.begin(); i != candidates.end(); ++i) {
			if ((*i)->second._state > S_Slave) {
				winner = i;
				break;
			}
		}

		return _elect_candidate(*winner);
	}

	bool elector::_forget_old_runners(const ptime& now) {
		vector<irunner> toremove;

		bool dirty = false;

		for (auto i = _runners.begin(); i != _runners.end(); ++i)
			if ((i->second._state <= S_Disconnected) &&
			    (now - i->second._last_seen > seconds(60*30))) // TODO: make configurable
				toremove.push_back(i);

		for (auto i = toremove.begin(); i != toremove.end(); ++i) {
			auto x = *i;
			LOG_INFO("Dropping %s (%s): Marked as disconnected/failed for at least 30 minutes.",
			         x->second._name.c_str(),
			         lexical_cast<string>(x->first).c_str());
			FOREACH(net::endpoint e, x->second._endpoints) {
				_emitter.remove_receiver(e);
			}
			_runners.erase(x);
			dirty = true;
		}
		return dirty;
	}

	void elector::start() {
		_emitter.start();
	}

	void elector::stop() {
		_emitter.stop();
	}

	void elector::update() {
		_emitter.update();

		// elect master that is already master
		dirtyflag dirty = _repromote_master();

		ptime now = microsec_clock::universal_time();
		// service information
		int npromoted = 0, nfailed = 0;
		dirty= _check_runner_health(now, npromoted, nfailed);

		// deelect disconnected master
		dirty= _check_master_health();

		// elect master if needed/allowed
		dirty= _election(now, npromoted);

		// forget runners that have been disconnected for some time
		dirty= _forget_old_runners(now);

		if (dirty.get())
			save_state();
	}

	bool elector::elect_node(runners::iterator const& i) {
		if (i->second._state > S_Stopped) {
			if (i->second._state == S_Master) {
				LOG_TRACE("%s (%s) is Master by popular opinion.",
				          i->second._name.c_str(), to_string(i->first).c_str());
				_master = i;
				_target_master = _runners.end();
				return true;
			}
			else if (i->second._state == S_Slave) {
				LOG_TRACE("%s (%s) is unilaterally promoted to Master.",
				          i->second._name.c_str(), to_string(i->first).c_str());
				_master = i;
				_target_master = _runners.end();
				return true;
			}
		}
		return false;
	}

	void elector::handle(message& m) {
		if (m._op != msg::base::HealthReport) {
			LOG_TRACE("Unexpected message type received from %s: %s",
			          to_string(m._from).c_str(), msg::type_to_string(m._op));
			return;
		}

		uuid sender_uuid = m._sender_uuid;
		net::endpoint from = m._from;
		const auto hr = m.body<msg::healthreport>();

		auto i = _runners.find(sender_uuid);
		runner_info* inf = 0;
		if (i != _runners.end()) {
			inf = &(i->second);
			inf->_endpoints.insert(from);
			_emitter.add_receiver(from);
			transition_runner(i, hr->_state);
		}
		else {
			LOG_INFO("New runner: %s.", hr->_name.c_str());
			auto ret = _runners.insert(make_pair(sender_uuid, runner_info()));
			inf = &(ret.first->second);
			inf->_endpoints.insert(from);
			_emitter.add_receiver(from);
			inf->_state = hr->_state;
			inf->_last_failed = ptime(min_date_time);
		}
		inf->_last_seen = microsec_clock::universal_time();
		if (inf->_state == S_Failed)
			inf->_last_failed = inf->_last_seen;
		inf->_uuid = sender_uuid;
		inf->_name = hr->_name;
		inf->_uptime = hr->_uptime;
		inf->_mode = hr->_mode;
		inf->_maintenance = hr->_maintenance;
		inf->_service_action = hr->_service_action;
		inf->_services = hr->_services;
		if (inf->_services.size() != hr->_services.size())
			LOG_TRACE("Service count mismatch!");

		save_state();
	}

	void elector::rpc_status(msg::request* rq,
	                         msg::response::values& response) {
		if (rq->_args.size() > 0) {
			auto i = find_runner(rq->_args[0].c_str());
			if (i == _runners.end()) {
				response["msg"] = "Error: Unknown runner";
			}
			else {
				response["redirect"] = to_string(i->second._endpoints.get());
				response["cmd"] = "local";
				response["args"] = rq->_args;
			}
		}
		else {
			struct local {
				static uuid nil_or_uuid(const runners::iterator& i, const runners::iterator& e) {
					return (i != e)? i->second._uuid : nil_uuid();
				}
			};
			response["master"] = local::nil_or_uuid(_master, _runners.end());
			response["target"] = local::nil_or_uuid(_target_master, _runners.end());
			response["manual-master"] = _manual_master_mode;
			response["maintenance"] = _emitter._nexus.cfg()._cluster_maintenance;
			int c = 0;

			FOREACH(const auto& i, _runners) {
				runner_info const& inf = i.second;
				strfmt<50> rcname("%x-name", c);
				strfmt<50> rcaddr("%x-addr", c);
				strfmt<50> rcuuid("%x-uuid", c);
				strfmt<50> rcstate("%x-state", c);
				strfmt<50> rcmode("%x-mode", c);
				strfmt<50> rcseen("%x-seen", c);
				strfmt<50> rclastfailed("%x-lastfailed", c);
				response[rcname.c_str()] = inf._name;
				response[rcaddr.c_str()] = to_string(inf._endpoints.get());
				response[rcuuid.c_str()] = inf._uuid;
				response[rcstate.c_str()] = (int)inf._state;
				response[rcmode.c_str()] = (int)inf._mode;
				response[rcseen.c_str()] = inf._last_seen;
				response[rclastfailed.c_str()] = inf._last_failed;

				strfmt<50> rcaction("%x-target-action", c);
				response[rcaction.c_str()] = (int)inf._service_action;

				vector<string> svcstate;
				FOREACH(const auto& s, inf._services)
					svcstate.push_back(s.to_string());
				strfmt<50> rcsvc("%x-services", c);
				response[rcsvc.c_str()] = svcstate;

				// TODO: is there a better way?
				FOREACH(const auto& n, _emitter._nexus.nodes()) {
					if (n._id == inf._uuid) {
						strfmt<50> rcflags("%x-flags", c);
						response[rcflags.c_str()] = nodeflags_to_string(n._flags);
					}
				}

				++c;
			}

			FOREACH(const auto& n, _emitter._nexus.nodes()) {
				if (_runners.count(n._id))
					continue;
				strfmt<50> rcname("%x-name", c);
				strfmt<50> rcaddr("%x-addr", c);
				strfmt<50> rcuuid("%x-uuid", c);
				strfmt<50> rcflags("%x-flags", c);
				strfmt<50> rcstate("%x-state", c);
				strfmt<50> rcmode("%x-mode", c);
				strfmt<50> rcseen("%x-seen", c);
				response[rcname.c_str()] = n._name;
				response[rcaddr.c_str()] = to_string(n._addrs.get());
				response[rcuuid.c_str()] = n._id;
				response[rcflags.c_str()] = nodeflags_to_string(n._flags);
				response[rcseen.c_str()] = n._last_seen;
				response[rcmode.c_str()] = (int)R_Active;
				response[rcstate.c_str()] = (n._id == _emitter._nexus.cfg()._uuid) ? S_Elector : S_Other;
				++c;
			}
		}
	}

	void elector::rpc_promote(msg::request* rq,
	                          msg::response::values& response) {
		LOG_INFO("Promoting new master: %s", rq->_args[0].c_str());
		if (rq->_args.size() != 1) {
			response["msg"] = "Error: wrong argument count (not 1)";
		}
		else if (promote_node(rq->_args[0].c_str())) {
			response["msg"] = "New master promoted.";
		}
		else {
			LOG_WARN("Promotion failed.");
			response["msg"] = "Error: Promotion failed.";
		}
	}

	void elector::rpc_demote(msg::request*, msg::response::values& response) {
		if (demote_master()) {
			LOG_WARN("Demoting current master.");
			LOG_WARN("No master will be elected until manually promoted.");
			response["msg"] = "Master demoted.";
		}
		else {
			LOG_WARN("Demotion failed.");
			response["msg"] = "Error: Masterless.";
		}
	}

	void elector::rpc_elect(msg::request*, msg::response::values& response) {
		LOG_WARN("Leaving manual master mode.");
		response["msg"] = "Leaving manual master mode.";
		_manual_master_mode = false;
	}

	void elector::rpc_failures(msg::request*, msg::response::values& response) {
		const int N = (int)MAX_FAILURES;
		const int nfailures = (int)_failures.size();
		strfmt<100> msg("Last %d failures", nfailures);
		response["msg"] = msg.c_str();
		int i = (std::max)(0, nfailures - N);
		for (; i < nfailures; ++i) {
			strfmt<64> id("%d", i);
			response[id.c_str()] = _failures[i].to_string();
		}
	}

	void elector::rpc_maintenance(msg::request* rq, msg::response::values& response) {
		// TODO
		if (rq->_args.size() != 1) {
			response["msg"] = "Error: bad argument (expecting on|off)";
		}
		else if (rq->_args[0] == "on") {
			LOG_WARN("Setting cluster in maintenance mode.");
			LOG_WARN("Services will not be started/stopped/monitored.");
			_emitter._nexus.cfg()._cluster_maintenance = true;
			response["msg"] = "Maintenance Mode ON";
		}
		else if (rq->_args[0] == "off") {
			LOG_WARN("Leaving cluster maintenance mode.");
			_emitter._nexus.cfg()._cluster_maintenance = false;
			response["msg"] = "Maintenance Mode OFF";
		}
		else {
			response["msg"] = "Error: bad argument (expecting on|off)";
		}
	}

	inline bool evaluate_candidate(elector::runners::iterator oldc,
	                               elector::runners::iterator newc) {
		return newc->second._state > oldc->second._state;
	}

	elector::runners::iterator elector::find_runner(const char* search_by) {
		auto candidate = _runners.end(), i = _runners.begin();
		for (; i != _runners.end(); ++i) {
			if (i->second._name == search_by) {
				if (candidate == _runners.end() ||
				    evaluate_candidate(candidate, i))
					candidate = i;
			}
		}
		try {
			uuid uid = string_generator()(search_by);
			for (i = _runners.begin(); i != _runners.end(); ++i) {
				if (i->second._uuid == uid) {
					if (candidate == _runners.end() ||
					    evaluate_candidate(candidate, i))
						candidate = i;
				}
			}
		}
		catch (std::runtime_error& e) {
		}

		if (candidate != _runners.end())
			return candidate;

		LOG_WARN("Not a name or UUID: %s", search_by);
		return _runners.end();
	}

	bool elector::promote_node(const char* name) {
		auto i = find_runner(name);
		if (i == _runners.end())
			return false;

		_manual_master_mode = false;
		_target_master = i;
		if (_target_master != _master) {
			_master = _runners.end();
			LOG_TRACE("%s: matched, switching master", name);
		}
		else {
			LOG_TRACE("%s: matched current master", name);
		}
		return true;
	}

	bool elector::demote_master() {
		if (_master != _runners.end()) {
			LOG_INFO("Switching to manual master mode; demoting any current master.");
			_manual_master_mode = true;
			_master = _runners.end();
			return true;
		}
		return false;
	}

	void elector::on_tick(message* m) {
		m->_sender_uuid = _emitter._nexus.cfg()._uuid;
		auto* su = m->set_body<msg::stateupdate>();
		m->_seqnr = _emitter._current_tick;

		su->_uptime = _emitter.uptime(_starttime);
		su->_maintenance = _emitter._nexus.cfg()._cluster_maintenance;

		if (_master != _runners.end()) {
			su->_master_uuid = _master->second._uuid;
			su->_master_last_seen = _master->second._last_seen;
			su->_master_name = _master->second._name;
			su->_master_addr = _master->second._endpoints.get();
		}
		else {
			su->_master_uuid = nil_uuid();
			su->_master_last_seen = ptime(min_date_time);
			su->_master_name = "";
			su->_master_addr = net::endpoint();
		}
	}

	void elector::save_state() {
		// write state to cluster
		{
			masterstate ms;
			ms._elector = _emitter._nexus.cfg()._uuid;
			if (_master != _runners.end()) {
				const auto& m = _master->second;
				ms._uuid = m._uuid;
				ms._name = m._name;
				ms._last_seen = m._last_seen;
				ms._addr = m._endpoints.get();
			}
			_emitter._nexus.set_masterstate(ms);
		}
	}

	void elector::load_state() {
		// fetch masterstate from cluster state
		masterstate ms = _emitter._nexus.get_masterstate();
		if (ms._uuid.is_nil())
			return;

		runners::iterator i;
		for (i = _runners.begin(); i != _runners.end(); ++i) {
			runner_info& r = i->second;
			if (r._uuid == ms._uuid &&
			    r._last_seen < ms._last_seen) {
				LOG_INFO("Cluster master: %s.", r._name.c_str());
				r.read(ms);
				_master = i;
				break;
			}
		}

		if (i == _runners.end()) {
			LOG_INFO("Cluster master: %s.", lexical_cast<string>(ms._uuid).c_str());
			runner_info master;
			master._endpoints.insert(ms._addr);
			master.read(ms);
			auto ret = _runners.insert(make_pair(ms._uuid, master));
			_emitter.add_receiver(ms._addr);
			_master = ret.first;
		}
	}

	void elector::register_failure(const runner_info& runner) {
		if (_failures.size() >= MAX_FAILURES)
			_failures.erase(_failures.begin());
		_failures.push_back(failure_info(runner._last_seen, runner._name, runner._uuid));
	}
}
