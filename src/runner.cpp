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
#include "runner.hpp"
#include "nexus.hpp"
#include <sstream>
#include <boost/bind.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "masterstate.hpp"

using namespace std;
using namespace boost;
using namespace boost::posix_time;

namespace koi {
	runner::elector_info::elector_info() {
		_uuid = uuids::nil_uuid();
		_last_seen = ptime(min_date_time);
		_master_uuid = uuids::nil_uuid();
		_uptime = 0;
		_maintenance = false;
	}

	runner::runner(nexus& route)
		: _emitter(route, route.cfg()._runner_tick_interval),
		  _elector() {
		_emitter._on_tick = bind(&runner::on_tick, this, _1);
		_state = S_Disconnected;
		_mode = R_Passive;
		_enabled = true;
		_warned_elector_lost = false;
		_quorum_lost = false;
		_failcount = 0;
		_last_transition = ptime(min_date_time);
	}

	runner::~runner() {
		LOG_INFO("Runner exiting.");
	}

	bool runner::init() {
		_starttime = microsec_clock::universal_time();
		return true;
	}

	bool runner::settings_changed(const settings& newcfg, const settings& oldcfg) {
		if (newcfg._runner == false) {
			LOG_INFO("Runner -> Elector forces restart");
			return false;
		}

		if (oldcfg._services_folder != newcfg._services_folder) {
			LOG_INFO("Services folder change forces restart");
			return false;
		}

		// force service manager to start new log proxy
		_services.toggle_logproxy();

		return true;
	}

	void runner::start() {
		_warned_elector_lost = false;
		_failcount = 0;
		_quorum_lost = false;
		_last_transition = ptime(min_date_time);
		_services.init(_emitter._nexus.cfg()._services_folder.c_str(), _emitter._nexus.cfg()._services_workingdir.c_str());
		switch_mode(R_Active, "Starting runner.");
		_emitter.start();

		LOG_INFO("Started runner.");
	}

	void runner::stop() {
		LOG_INFO("Stopping runner.");

		_services.demote();
		_services.wait_for_demote(in_maintenance_mode());

		_emitter.immediate_tick();
		_emitter._nexus.io().poll();

		_services.stop();
		_services.wait_for_stop(in_maintenance_mode());

		_emitter.immediate_tick();
		_emitter._nexus.io().poll();

		switch_mode(R_Passive, "Stopping runner.");
		transition(S_Stopped, "Runner shutting down.");

		_emitter.clear_receivers();

		_emitter.immediate_tick();
		_emitter.stop();
	}

	void runner::_check_timeouts(const ptime& now) {
		const settings& cfg = _emitter._nexus.cfg();

		// check timeouts! should we go from connected -> disconnected
		if (_state > S_Stopped) {

			// loss of elector
			if (now - _elector._last_seen > microseconds(cfg._runner_elector_gone_time)) {
				if (_state == S_Master) {
					transition(S_Slave, "Elector gone. Demoting self.");
				}
			}
			else if (now - _elector._last_seen > microseconds(cfg._runner_elector_lost_time)) {
				if (!_warned_elector_lost) {
					LOG_WARN("Elector lost.");
					if (_state > S_Slave) {
						LOG_WARN("Demote in %d seconds (%d minutes).",
						         (int)(cfg._runner_elector_gone_time/1e6),
						         (int)(cfg._runner_elector_gone_time/1e6/60));
					}
					_warned_elector_lost = true;
				}
			}

			// loss of quorum
			if (_state > S_Slave && !_emitter._nexus.has_quorum()) {
				if (!_quorum_lost) {
					_quorum_lost = true;
					_quorum_lost_time = now;
					LOG_WARN("Loss of quorum! Number of visible nodes below %d limit.", cfg._cluster_quorum);
				}
				else if (now - _quorum_lost_time > microseconds(cfg._quorum_demote_time)) {
					transition(S_Slave, "Quorum lost. Demoting self.");
				}
			}
			else if (_quorum_lost) {
				_quorum_lost = false;
			}
		}
	}

	void runner::_log_transition_state() {
		if (koi::debug_mode) {
			stringstream ss;
			ss << "Transition check;"
			   << (_enabled ? " enabled " : " not enabled ")
			   << " state " << state_to_string(_state)
			   << " uuid " << _emitter._nexus.cfg()._uuid
			   << " master " << _elector._master_uuid
			   << " elector addr " << _emitter._nexus.get_elector();
			LOG_INFO("%s", ss.str().c_str());
		}
	}

	void runner::_check_elector_transition() {
		_log_transition_state();

		if (!_enabled) {
			if (_state != S_Failed && _state != S_Stopped) {
				transition(S_Stopped, "Manually stopped.");
			}
		}
		else if (_elector._master_uuid == _emitter._nexus.cfg()._uuid &&
		         _state >= S_Stopped) {
			if (_state < S_Live) {
				transition(S_Live, "Elector assigned master.");
			}
			else {
				transition(S_Master, "Elector assigned master.");
			}
		}
		else if (_state > S_Slave) {
			transition(S_Slave, "Demoted by elector.");
		}
		else if (_state == S_Slave || _state == S_Live) {
			// do nothing
		}
		else if (_state >= S_Disconnected) {
			transition(S_Live, "Connected to elector.");
		}
	}

	void runner::_update_elector_state(const ptime& now) {
		if (now - _elector._last_seen >= microseconds(_emitter._nexus.cfg()._runner_elector_lost_time))
			return;

		_warned_elector_lost = false;
		if (_elector._uptime > _emitter._nexus.cfg()._elector_startup_tolerance/units::milli) {
			_check_elector_transition();
		}
		else if (!_enabled && (_state == S_Disconnected || _state > S_Stopped)) {
			transition(S_Stopped, "Manually stopped.");
		}
		else if (_enabled && (_state == S_Disconnected || _state == S_Stopped)) {
			transition(S_Live, "Connected to elector (low uptime).");
		}
	}

	void runner::_check_recovery(const ptime& now) {
		const settings& cfg = _emitter._nexus.cfg();

		if (_state == S_Failed && _failcount < cfg._service_auto_recover) {
			uint64_t time = cfg._auto_recover_time;
			const int factor = cfg._auto_recover_wait_factor;
			if (factor >= 2 && factor <= AUTO_RECOVER_MAX_FACTOR) {
				const int cfac = static_cast<int>(pow((float)factor, (int)_failcount));
				time *= (uint64_t)(std::max)(1, cfac);
			}

			if (now - _last_transition > microseconds(time)) {
				LOG_INFO("Increasing failcount after %d seconds (now %d); attempting recovery", (int)(time/units::micro), _failcount+1);
				++_failcount;
				forget_failure();
			}
		} // reset failcount after X seconds
		else if (cfg._service_auto_recover > 0 &&
		         _failcount >= cfg._service_auto_recover &&
		         now - _last_transition > microseconds(cfg._service_failcount_reset_time)) {
			LOG_INFO("Resetting failcount (was %d)", _failcount);
			_failcount = 0;
			forget_failure();
		}
	}

	bool runner::in_maintenance_mode() const {
		return _emitter._nexus.cfg()._cluster_maintenance;
	}

	void runner::_check_service_status() {
		// run service status check (if the time has come)
		const ServicesStatus status = _services.update(
			service_events(_emitter._nexus.cfg()), _state,
			_emitter._nexus.cfg()._status_interval,
			_emitter._nexus.cfg()._state_update_interval,
			in_maintenance_mode());
		if (status == Status_Error) {
			LOG_ERROR("* Service failure detected (failcount=%d) *", _failcount);
			transition(S_Failed, "Stopping all services running on this node.");
		}
		else if (_state == S_Live && (status >= Status_Promotable)) {
			transition(S_Slave, "Status is promotable");
		}
		else if (_state > S_Live && (status < Status_Promotable)) {
			transition(S_Live, "Node is not promotable");
		}
	}

	void runner::update() {
		const ptime now = microsec_clock::universal_time();

		// update receivers based on ports elector
		{
			net::endpoint prev;
			if (_emitter._receivers.size())
				prev = *_emitter._receivers.begin();
			_emitter.clear_receivers();
			net::endpoint a = _emitter._nexus.get_elector();
			if (a != net::endpoint()) {
				if (a != prev) {
					LOG_INFO("Elector endpoint changed: %s -> %s", to_string(prev).c_str(), to_string(a).c_str());
				}
				_emitter.add_receiver(a);
			}
		}

		_emitter.update();

		_check_timeouts(now);
		if (!_services.is_disabled()) {
			_update_elector_state(now);
			_check_recovery(now);
		}
		_check_service_status();
	}

	void runner::handle(message& m) {
		if (m._op != msg::base::StateUpdate) {
			LOG_TRACE("Unexpected message type received from %s: %s",
			          to_string(m._from).c_str(), msg::type_to_string(m._op));
			return;
		}

		if (_elector._uuid != m._sender_uuid) {
			// force new sequence nr
			LOG_WARN("Elector has changed UUID: %s -> %s", lexical_cast<string>(_elector._uuid).c_str(),
			         lexical_cast<string>(m._sender_uuid).c_str());
			LOG_WARN("Elector endpoint: %s", to_string(m._from).c_str());
			_sequence.set(m._sender_uuid, m._seqnr);
		}

		const auto su = m.body<msg::stateupdate>();
		if (_elector._uuid == m._sender_uuid) { // same node
                        if ((int64_t)(su->_uptime - _elector._uptime) < 0) { // elector restarted
                                LOG_WARN("Elector was restarted");
                                _sequence.set(m._sender_uuid, m._seqnr);
                        }
                        else if (!_sequence.check(_elector._uuid, m._seqnr)) { // out of order
		                LOG_WARN("State update out of sequence: %d", (int)m._seqnr);
		                return;
                        }
		}

		_elector._uuid = m._sender_uuid;
		_elector._last_seen = microsec_clock::universal_time();
		_elector._master_uuid = su->_master_uuid;
		_elector._uptime = su->_uptime;

		if (_elector._uuid != _emitter._nexus.cfg()._uuid) {
			// Update the masterstate, so that in case of failover,
			// we're up to date. Don't do this if we're the elector
			// since the elector will be updating the masterstate.
			masterstate ms;
			ms._uuid = su->_master_uuid;
			ms._elector = _elector._uuid;
			ms._name = su->_master_name;
			ms._last_seen = su->_master_last_seen;
			ms._addr = su->_master_addr;
			_emitter._nexus.set_masterstate(ms);
		}
	}

	void runner::on_tick(message* m) {
		m->_sender_uuid = _emitter._nexus.cfg()._uuid;
		auto hr = m->set_body<msg::healthreport>();
		m->_seqnr = _emitter._current_tick;
		hr->_name = _emitter._nexus.cfg()._name;
		hr->_uptime = _emitter.uptime(_starttime);
		hr->_state = _state;
		hr->_mode = _mode;
		hr->_maintenance = in_maintenance_mode();

		_services.report(hr);

		FOREACH(const auto& si, hr->_services)
			if (si._failed)
				transition(S_Failed, "discovered failed services");
	}

	void runner::forget_failure() {
		_services.forget_failure();
		if (_state == S_Failed)
			transition(S_Disconnected, "Recovering from service failure");
	}

	void runner::transition(State new_state, const char* why) {
		if (new_state == _state)
			return;

		LOG_INFO("Transition[%s -> %s]: %s", state_to_string(_state),
		         state_to_string(new_state), why);

		// update services
		switch (new_state) {
		case S_Disconnected:
			if (_state > S_Stopped)
				_services.stop();
			break;
		case S_Stopped:
			if (_state > S_Stopped)
				_services.stop();
			break;
		case S_Live:
			if (_state > S_Slave)
				_services.demote();
			else if (_state > S_Failed)
				_services.start();
			break;
		case S_Slave:
			if (_state > S_Slave)
				_services.demote();
			else if (_state > S_Failed)
				_services.start();
			break;
		case S_Master:
			if (_state > S_Failed)
				_services.promote();
			break;
		case S_Failed:
			_services.fail();
			break;
		default:
			break;
		}

		_state = new_state;
		_last_transition = microsec_clock::universal_time();
	}

	void runner::switch_mode(RunnerMode new_mode, const char* why) {
		if (new_mode == _mode)
			return;

		LOG_INFO("Mode[%s -> %s]: %s", mode_to_string(_mode),
		         mode_to_string(new_mode), why);

		_mode = new_mode;
	}

	void runner::rpc_start(msg::request*, msg::response::values& out) {
		if (_enabled) {
			out["msg"] = "Runner already started.";
		}
		else {
			_enabled = true;
			out["msg"] = "Runner started.";
		}
	}

	void runner::rpc_stop(msg::request*, msg::response::values& out) {
		if (_enabled) {
			_enabled = false;
			out["msg"] = "Runner stopped.";
		}
		else {
			out["msg"] = "Runner already stopped.";
		}
	}

	void runner::rpc_recover(msg::request*, msg::response::values& out) {
		const bool in_fail_state = _state == S_Failed;
		forget_failure();
		_failcount = 0;
		if (in_fail_state) {
			out["msg"] = "Failcount reset, leaving Failed state.";
		}
		else {
			out["msg"] = "Failcount reset.";
		}
	}
}
