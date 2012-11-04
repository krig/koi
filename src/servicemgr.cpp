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
#include <set>
#include "servicemgr.hpp"
#include "cmd.hpp"
#include "os.hpp"
#include "globber.hpp"
#include <boost/date_time/posix_time/posix_time.hpp>
#include <signal.h>

using namespace std;
using namespace boost;
using namespace boost::posix_time;

namespace {
	using namespace koi;

	const char* get_servicename(const char* name) {
		if (isdigit(*name)) {
			name++;
			if (isdigit(*name))
				name++;
		}
		if (*name=='-')
			name++;
		return name;
	}

	int get_serviceprio(const char* name) {
		char tmp[3] = {0,0,0};
		for (int i = 0; i < 2; ++i)
			if (isdigit(name[i]))
				tmp[i] = name[i];
		if (tmp[0] != 0) {
			return (int)strtol(tmp, 0, 10);
		}
		return service_manager::service::NO_PRIORITY;
	}

	char services_folder[512] = KOI_CONFIG_SERVICES;
	char services_workingdir[512] = KOI_CONFIG_SERVICES;

}

namespace koi {
	const int service_manager::service::NO_PRIORITY = -1;

	void service_manager::init(const char* servicesdir, const char* workingdir) {
		LOG_TRACE("Initializing service manager with service folder: %s, working dir: %s.",
		          servicesdir, workingdir);
		strmcpy(services_folder, servicesdir, sizeof(services_folder));
		strmcpy(services_workingdir, workingdir, sizeof(services_workingdir));
		_last_check = ptime(min_date_time);
		_target_action = Svc_Stop;
		_logproxy.create();
		if (!os::path::makepath(services_workingdir)) {
			LOG_ERROR("%s does not exist, this is fatal!", services_workingdir);
		}
		LOG_TRACE("Service manager initialized.");
	}

	service_manager::~service_manager() {
		LOG_TRACE("Terminating service manager.");
		stop();
		wait_for_shutdown();
		LOG_TRACE("Service manager terminated.");
	}

	string service_manager::to_string() const {
		return status_summary(ptime(min_date_time), false);
	}

	string service_manager::status_summary(const ptime& now, bool details) const {
		stringstream ss;

		bool comma = false;
		FOREACH(const auto& svc, _services) {
			const service& s = svc.second;

			service_info inf;
			inf._name = s._name;
			inf._state = s._state;
			inf._event = s._event ? s._event->_name : "none";
			inf._failed = s.is_failed();
			if (comma)
				ss << ", ";
			else
				comma = true;

			ss << inf.to_string();

			if (details && s._running.is_active() && (s._event != 0)) {
				const ptime start_time = s._running.started_at;
				const time_duration td = now - start_time;

				if (!td.is_negative())
					ss << "["
					   << to_simple_string(td)
					   << "]";
			}
		}
		return ss.str();
	}

	void service_manager::toggle_logproxy() {
		_logproxy.close();
		_logproxy.create();
	}

	bool service_manager::update(const service_events& events, State state, uint64_t status_interval, uint64_t state_update_interval, bool maintenance_mode) {
		ptime now = microsec_clock::local_time();

		// Update environment variables used by services
		::setenv("KOI_IS_PROMOTED", (_target_action == Svc_Promote) ? "1" : "0", 1);
		::setenv("KOI_STATE", state_to_string(state), 1);

		// run monitor checks if it's time
		if (now - _last_check > microseconds(status_interval)) {
			_last_check = now;

			if (maintenance_mode) {
				LOG_TRACE("Skipping status check (maintenance mode)");
			}
			else if (!status(events)) {
				return false;
			}
		}

		if (maintenance_mode) {
			return true;
		}

		if (!update_states(state_update_interval)) {
			LOG_TRACE("servicemgr::update_states() failure");
			return false;
		}

		return true;
	}

	void service_manager::start() {
		LOG_INFO("service manager: start");
		if (_target_action < Svc_Start)
			_target_action = Svc_Start;
	}

	void service_manager::stop() {
		LOG_INFO("service manager: stop");
		if (_target_action > Svc_Stop)
			_target_action = Svc_Stop;
	}

	void service_manager::promote() {
		LOG_INFO("service manager: promote");
		_target_action = Svc_Promote;
	}

	void service_manager::demote() {
		LOG_INFO("service manager: demote");
		if (_target_action > Svc_Demote)
			_target_action = Svc_Demote;
	}

	void service_manager::fail() {
		LOG_INFO("service manager: fail");
		_target_action = Svc_Fail;
	}

	bool service_manager::status(service_events const& events) {
		const ptime now = microsec_clock::local_time();
		update_list(events);
		const bool ok = verify_states(now);
		const string ssum = status_summary(now);
		const string nows = lexical_cast<string>(now);
		LOG_INFO("%s [%s]: %s", (ok ? "Status" : "FAILED"), nows.c_str(), ssum.c_str());
		return ok;
	}

	void service_manager::forget_failure() {
		FOREACH(auto& s, _services) {
			if (s.second._state < Svc_Stopped)
				s.second._state = Svc_Stopped;
			s.second._service_flags &= ~service::IS_FAILED;
		}
	}

	void service_manager::_remove_services() {
		// check if any services have changed
		vector<services::iterator> removed;
		for (auto i = _services.begin(); i != _services.end(); ++i) {
			service& s = i->second;
			if (!s.update_info()) {
				LOG_INFO("-service: %s [%s]", s._name.c_str(), s._path.c_str());
				removed.push_back(i);
			}
		}

		FOREACH(auto i, removed)
			_services.erase(i);
	}

	namespace {
		void warn_ignore(const char* reason, const char* name, std::set<string>& ignored) {
			if (ignored.find(name) == ignored.end()) {
				LOG_WARN("%s: %s", reason, name);
				ignored.insert(name);
			}
		}

		bool legal_service_name(const char* name, size_t len, std::set<string>& ignored) {
			if (len == 0) {
				return false;
			}
			else if (strchr(name, '.') != 0) {
				warn_ignore("Service with name containing '.' discovered and ignored", name, ignored);
				return false;
			}
			else if (name[len-1] == '~') {
				warn_ignore("Service with name ending in '~' discovered and ignored", name, ignored);
				return false;
			}
			else if (name[0] == '#' && name[len-1] == '#') {
				warn_ignore("Service with name of form '#name#' discovered and ignored", name, ignored);
				return false;
			}
			return true;
		}
	}

	void service_manager::update_list(service_events const& events) {
		_remove_services();

		// list available services
		string globstr = services_folder;
		globstr += "/*";
		globber g(globstr.c_str());
		for (size_t i = 0; i < g.size(); ++i) {
			if (!os::path::isdir(g[i]) && !os::path::isexec(g[i]))
				continue;

			const char* name = get_servicename(os::path::basename(g[i]));
			const size_t name_len = strlen(name);
			if (legal_service_name(name, name_len, _ignored_services) == false)
				continue;

			if (_services.find(name) != _services.end())
				continue;

			int prio = get_serviceprio(os::path::basename(g[i]));

			service s(events, name, g[i]);
			s._state = Svc_Stopped;
			s._priority = prio;
			LOG_INFO("+service[%d]: %s", s._priority, name);
			s.update_info();
			_services[name] = s;
		}
	}

	bool service_manager::verify_states(ptime now) {
		bool allwell = true;
		for (auto i = _services.begin(); i != _services.end(); ++i) {
			service& s = i->second;
			if (s._running.is_active()) {
				if (!s._running.query_complete()) {
					LOG_TRACE("%s:%s is blocking status check...", s._name.c_str(), s._event->_name.c_str());

					if (now - s._running.started_at > microseconds(s._event->_timeout)) {
						string t = to_simple_string(now - s._running.started_at);
						LOG_WARN("Command '%s' timed out after %s", s._event->_name.c_str(),
						         t.c_str());
						s._running.kill(SIGKILL);
						s.report_timeout();
						s.unrun();
						allwell = false;
					}
				}
				else if (check_exitcode(s)) {
					s.unrun();
					s.status(_logproxy.inpipe);
				}
				else {
					_service_failed(s);
					allwell = false;
				}
			}
			else {
				s.status(_logproxy.inpipe);
			}
		}
		return allwell;
	}

	void service_manager::_service_failed(service& s) {
		s.report_fail();
		s.fail(_logproxy.inpipe);
	}

	void service_manager::wait_for_demote(bool maintenance_mode) {
		ptime begin_wait = microsec_clock::local_time();

		bool at_target_state, empty_loop, first_loop = true;

		if (maintenance_mode || (_target_action != Svc_Demote))
			return;

		do {
			if (microsec_clock::local_time() - begin_wait > seconds(360)) {
				LOG_ERROR("Timeout: demote is taking too long!");
				break;
			}

			at_target_state = true;
			empty_loop = true;
			update_states((1e6)/10, first_loop);
			first_loop = false;

			FOREACH(auto& svc, _services) {
				service& s = svc.second;
				if (s._running.is_active()) {
					LOG_INFO("Waiting for %s:%s to complete...", s._name.c_str(), s._event->_name.c_str());
					s._running.wait();
					s.unrun();
					empty_loop = false;
				}
				if (!resolves(s._state, Svc_Demote)) {
					at_target_state = false;
					usleep(3e5);
				}
			}
		} while (!empty_loop || !at_target_state);
	}

	void service_manager::wait_for_stop(bool /*maintenance_mode*/) {
		// skip for now
	}

	void service_manager::wait_for_shutdown() {
		ptime begin_wait = microsec_clock::local_time();

		bool at_target_state;
		bool empty_loop;
		bool first_loop = true;
		do {
			// TODO: make configurable
			if (microsec_clock::local_time() - begin_wait > seconds(360)) {
				FOREACH(auto& svc, _services) {
					service& s = svc.second;
					if (s._running.is_active()) {
						LOG_ERROR("Timeout: SIGKILLing shutdown process: %s", s._event->_name.c_str());
						s._running.kill(SIGKILL);
					}
					if (s._state > Svc_Stopped) {
						LOG_ERROR("Timeout: service still active: %s", s._path.c_str());
					}
				}
				LOG_ERROR("Timeout: aborting shutdown process!");
				break;
			}

			at_target_state = true;
			empty_loop = true;
			update_states((1e6)/10, first_loop);
			first_loop = false;
			FOREACH(auto& svc, _services) {
				service& s = svc.second;
				if (s._running.is_active()) {
					LOG_INFO("Waiting for %s:%s to complete...", s._name.c_str(), s._event->_name.c_str());
					s._running.wait();
					s.unrun();

					empty_loop = false;
				}
				if (!resolves(s._state, Svc_Stop) && !s.is_failed()) {
					at_target_state = false;
					usleep(3e5);
				}
			}
		} while (!empty_loop || !at_target_state);
	}

	bool service_manager::check_exitcode(service& s) const {
		enum StatusExitCodes {
			EC_OK = 0,
			EC_Error = 1,
			EC_Master = 90,
			EC_Slave = 91,
			EC_Stopped = 92
		};
		const int ecode = s._running.exitcode;
		if (s._event &&
		    (s._event->_name == "status") &&
		    (ecode >= EC_Master) &&
		    (ecode <= EC_Stopped)) {
			switch (ecode) {
			case EC_Master:
				if (s._state < Svc_Demoting)
					s._state = Svc_Promoted;
				return true;
			case EC_Slave:
				if (s._state == Svc_Promoted ||
				    s._state < Svc_Stopping)
					s._state = Svc_Started;
				return true;
			case EC_Stopped:
			default:
				if (s._state > Svc_Starting)
					s._state = Svc_Stopped;
				return true;
			}
		}
		return ecode == 0;
	}

	bool service_manager::complete_transition(ptime now, service& s) {
		if (s._running.is_active()) {
			if (s._running.query_complete()) {
				if (check_exitcode(s)) {
					const string t = to_simple_string(now - s._running.started_at);
					LOG_TRACE("%s:%s OK (time: %s).",
					          s._name.c_str(),
					          s._event->_name.c_str(),
					          t.c_str());
					s.unrun();
					s.complete_transition();
				}
				else {
					LOG_TRACE("%s:%s failed, exitcode: %d",
					          s._name.c_str(),
					          s._event->_name.c_str(),
					          s._running.exitcode);
					_service_failed(s);
					return false;
				}
			}
			else {
				// check for timeouts
				if (now - s._running.started_at > microseconds(s._event->_timeout)) {
					const string t = to_simple_string(now - s._running.started_at);
					LOG_WARN("Command '%s' timeout after: %s",
					         s._event->_name.c_str(),
					         t.c_str());
					s._running.kill(SIGKILL);
					s.unrun();
					_service_failed(s);
					return false;
				}
				// check for overriding actions (is starting -> should be stopping)
				switch (_target_action) {
				case Svc_Stop:
				case Svc_Fail: {
					if (s._state == Svc_Starting) {
						LOG_WARN("Overriding starting transition to stop immediately: %s", s._name.c_str());
						s._running.kill(SIGKILL);
						s.unrun();
						if (s._state == Svc_Starting) {
							s.transition(Svc_Started);
							LOG_WARN("Forcing stop");
							s.stop(_logproxy.inpipe);
						}
					}
				} break;
				default: break;
				};
			}
		}
		else {
			const string t = to_simple_string(now - s._running.started_at);
			LOG_TRACE("%s:%s OK (time: %s).",
			          s._name.c_str(),
			          s._event->_name.c_str(),
			          t.c_str());
			s.unrun();
			s.complete_transition();
		}
		return true;
	}

	bool service_manager::_update_failed_service(service& s) {
		if (s._state > Svc_Stopped) {
			if (!s.stop(_logproxy.inpipe)) {
				LOG_TRACE("stop() failed");
				if (!s.fail(_logproxy.inpipe)) {
					LOG_TRACE("fail() failed");
				}
				return false;
			}
		}
		else if (s._state <= Svc_Stopped) {
			if (!s.fail(_logproxy.inpipe)) {
				LOG_TRACE("fail() failed");
				return false;
			}
		}
		return true;
	}

	bool service_manager::_update_stopped_service(service& s) {
		if (_target_action > Svc_Stop) {
			if (allow_start(s) && !s.start(_logproxy.inpipe)) {
				LOG_TRACE("start() failed");
				return false;
			}
		}
		return true;
	}

	bool service_manager::_update_started_service(service& s) {
		if (_target_action == Svc_Stop || _target_action == Svc_Fail) {
			if (allow_stop(s) && !s.stop(_logproxy.inpipe)) {
				LOG_TRACE("stop() failed");
				return false;
			}
		}
		else if (_target_action == Svc_Promote) {
			if (allow_promote(s) && !s.promote(_logproxy.inpipe)) {
				LOG_TRACE("promote() failed");
				return false;
			}
		}
		return true;
	}

	bool service_manager::_update_promoted_service(service& s) {
		if (_target_action == Svc_Demote ||
		    _target_action == Svc_Stop ||
		    _target_action == Svc_Fail) {
			if (allow_demote(s) && !s.demote(_logproxy.inpipe)) {
				LOG_TRACE("demote() failed");
				return false;
			}
		}
		return true;
	}

	bool service_manager::_update_service_state(service& s) {
		if (s.is_failed())
			return _update_failed_service(s);
		switch (s._state) {
		case Svc_Promoted: return _update_promoted_service(s);
		case Svc_Started:  return _update_started_service(s);
		case Svc_Stopped:  return _update_stopped_service(s);
		default:
			break;
		}
		return true;
	}

	bool service_manager::_update_service(const ptime& now, service& s) {
		if (s.in_transition()) {
			return complete_transition(now, s);
		}
		else if (s._running.is_active() && s._running.query_complete()) {
			if (check_exitcode(s)) {
				s.unrun();
			}
			else {
				LOG_TRACE("Command complete, returned: %d", s._running.exitcode);
				_service_failed(s);
				return false;
			}
		}
		else if (s._state != Svc_Failed && !resolves(s._state, _target_action)) {
			return _update_service_state(s);
		}

		return true;
	}

	bool service_manager::update_states(uint64_t state_update_interval, bool force) {
		ptime now = microsec_clock::local_time();
		if (!force && (now - _last_update_states < microseconds(state_update_interval)))
			return true;
		_last_update_states = now;

		//string ts = to_simple_string(now);
		//LOG_TRACE("%s: [target-state: %s]", ts.c_str(), state_string(_target_state));

		bool allwell = true;
		FOREACH(auto& s, _services) {
			allwell = _update_service(now, s.second) && allwell;
		}
		return allwell;
	}

	void service_manager::report(msg::healthreport* hr) {
		hr->_service_action = _target_action;
		hr->_services.clear();
		for (auto i = _services.begin(); i != _services.end(); ++i) {
			service& s = i->second;
			service_info inf;
			inf._name = s._name;
			inf._state = s._state;
			inf._event = s._event->_name;
			inf._failed = s.is_failed();
			hr->_services.push_back(inf);
		}
	}

	bool service_manager::allow_start(const service& a) const {
		if (a._priority == service::NO_PRIORITY)
			return true;
		for (services::const_iterator i = _services.begin(); i != _services.end(); ++i) {
			const service& b = i->second;
			if (a._name == b._name)
				continue;
			if ((a._priority > b._priority) &&
			    (b._state < Svc_Started))
				return false;
		}
		return true;
	}

	bool service_manager::allow_stop(const service& a) const {
		if (a._priority == service::NO_PRIORITY)
			return true;
		for (services::const_iterator i = _services.begin(); i != _services.end(); ++i) {
			const service& b = i->second;
			if (a._name == b._name)
				continue;
			if ((a._priority < b._priority) &&
			    (b._state > Svc_Stopped))
				return false;
		}
		return true;
	}

	bool service_manager::allow_promote(const service& a) const {
		if (a._priority == service::NO_PRIORITY)
			return true;
		for (services::const_iterator i = _services.begin(); i != _services.end(); ++i) {
			const service& b = i->second;
			if (a._name == b._name)
				continue;
			if ((a._priority > b._priority) &&
			    (b._state < Svc_Promoted))
				return false;
		}
		return true;
	}

	bool service_manager::allow_demote(const service& a) const {
		if (a._priority == service::NO_PRIORITY)
			return true;
		for (services::const_iterator i = _services.begin(); i != _services.end(); ++i) {
			const service& b = i->second;
			if (a._name == b._name)
				continue;
			if ((a._priority < b._priority) &&
			    (b._state > Svc_Started))
				return false;
		}
		return true;
	}

	service_manager::service::service() : _events(),
	                                      _running(),
	                                      _event(0),
	                                      _name(),
	                                      _path(),
	                                      _priority(NO_PRIORITY),
	                                      _state(Svc_Stopped),
	                                      _service_flags(0) {
	}

	service_manager::service::service(service_events const& events, const char* name, const char* path) : _events(events),
		                                _running(),
		                                _event(events("none")),
		                                _name(name),
		                                _path(path),
		                                _priority(NO_PRIORITY),
		                                _state(Svc_Stopped),
		                                _service_flags(0) {
	}

	namespace {
		inline const char* combine_path(string& buf, const char* root, const char* suffix) {
			buf = root;
			buf += suffix;
			return buf.c_str();
		}
	}

	bool service_manager::service::_read_service_flags(uint32_t& new_info, const char* path) {
		string root = path;
		root += "/";
		if (os::path::isdir(path)) {
			string tmp;
			tmp.reserve(root.length() + 10);

			new_info |= (os::path::isexec(combine_path(tmp, root.c_str(), "start")) ? HAS_START : 0);
			new_info |= (os::path::isexec(combine_path(tmp, root.c_str(), "stop")) ? HAS_STOP : 0);
			new_info |= (os::path::isexec(combine_path(tmp, root.c_str(), "status")) ? HAS_STATUS : 0);
			new_info |= (os::path::isexec(combine_path(tmp, root.c_str(), "promote")) ? HAS_PROMOTE : 0);
			new_info |= (os::path::isexec(combine_path(tmp, root.c_str(), "demote")) ? HAS_DEMOTE : 0);
			new_info |= (os::path::isexec(combine_path(tmp, root.c_str(), "failed")) ? HAS_FAIL : 0);

			// note this calls exists(), not isexec()
			new_info |= (os::path::exists(combine_path(tmp, root.c_str(), "disabled")) ? IS_DISABLED : 0);

			return true;
		}
		else if (os::path::isexec(path)) {
			new_info = HAS_START|HAS_STOP|HAS_STATUS|HAS_PROMOTE|HAS_DEMOTE|HAS_FAIL|SINGLE_SCRIPT;
			return true;
		}
		return false;
	}

	bool service_manager::service::update_info() {
		const uint32_t old_info = _service_flags;
		uint32_t new_info = (_service_flags&IS_FAILED)?IS_FAILED:0;
		bool has_service = false;

		has_service = _read_service_flags(new_info, _path.c_str());

		const uint32_t changes = new_info ^ old_info;
		if (changes != 0) {
			const char* flagNames[] = {
				"start", "stop", "status", "promote", "demote",
				"fail", "single", "isfailed", "disabled"
			};
			stringstream ss;
			for (size_t i = 0; i < ASIZE(flagNames); ++i) {
				if (changes & (1<<i)) {
					bool added = (new_info & (1<<i));
					ss << flagNames[i] << (added ? "+" : "-");
				}
			}
			LOG_INFO("service changes: %s: %s", _name.c_str(), ss.str().c_str());

			if (new_info & IS_DISABLED)
				LOG_WARN("Service is disabled: %s", _name.c_str());

			_service_flags = new_info;
		}
		return has_service;
	}

	void service_manager::service::report_fail() {
		LOG_ERROR("%s:%s returned %d!", _name.c_str(), _event->_name.c_str(), _running.exitcode);
		_service_flags |= IS_FAILED;
	}

	void service_manager::service::report_timeout() {
		LOG_ERROR("%s:%s timed out!", _name.c_str(), _event->_name.c_str());
		_service_flags |= IS_FAILED;
	}

	void service_manager::service::unrun() {
		_running.pid = 0;
		_running.exitcode = 0;
		_event = _events("none");
	}

	bool service_manager::service::launch_command(const char* c, int inpipe) {
		string wd(services_workingdir);
		char av0[512] = {0};
		char av1[128] = {0};
		char* av[3] = { av0, av1, 0 };
		if (!single_script()) {
			snprintf(av0, 512, "%s/%s", _path.c_str(), c);
			av[1] = 0;
		}
		else {
			strmcpy(av0, _path.c_str(), sizeof(av0));
			strmcpy(av1, c, sizeof(av1));
			av[2] = 0;
		}
		if (!single_script()) {
			wd = _path;
		}
		_event = _events(c);
		LOG_TRACE("Executing: %s (%s)", av0, av1);
		_running.begin_pipe_stdout_to(av, wd.c_str(), inpipe);
		return true;
	}

	bool service_manager::service::start(int inpipe) {
		if (_running.is_active()) {
			LOG_WARN("%s:start(): action running: %s", _name.c_str(), _event->_name.c_str());
			return true; // defer action
		}

		if (is_disabled())
			return true;

		if (is_failed() || _state <= Svc_Failing) {
			LOG_ERROR("%s:start(): cannot start a failed action", _name.c_str());
			return false;
		}
		if (_state < Svc_Starting) {
			if (_service_flags & HAS_START) {
				transition(Svc_Starting);
				if (!launch_command("start", inpipe)) {
					LOG_WARN("Failed to execute %s%s", _path.c_str(), "/start");
					return false;
				}
			}
			else {
				transition(Svc_Started);
			}
		}
		return true;
	}
	bool service_manager::service::stop(int inpipe) {
		if (_running.is_active()) {
			LOG_WARN("%s:stop(): action running: %s", _name.c_str(),
			         (_event ? _event->_name.c_str() : "none"));

			// if we are in the middle of starting or promoting
			// or statusing
			// don't wait for it to complete
			if (_event && (_event->_name == "start" ||
			               _event->_name == "promote" ||
			               _event->_name == "status")) {
				LOG_WARN("%s:stop(): aborting running action: %s", _name.c_str(), _event->_name.c_str());
				_running.kill(SIGKILL);
			}
			else
				return true; // defer action
		}
		if (_state >= Svc_Starting) {
			if (_service_flags & HAS_STOP) {
				transition(Svc_Stopping);
				if (!launch_command("stop", inpipe)) {
					LOG_WARN("Failed to execute %s%s", _path.c_str(), "/stop");
					return false;
				}
			}
			else {
				transition(Svc_Stopped);
			}
		}
		return true;
	}
	bool service_manager::service::status(int inpipe) {
		if (_running.is_active()) {
			LOG_WARN("%s:status(): action running: %s", _name.c_str(), _event->_name.c_str());
			return true; // defer action
		}
		if (_state >= Svc_Started) {
			if (is_disabled())
				return stop(inpipe);

			if (_service_flags & HAS_STATUS) {
				if (!launch_command("status", inpipe)) {
					LOG_WARN("Failed to execute %s%s", _path.c_str(), "/status");
					return false;
				}
			}
		}
		return true;
	}
	bool service_manager::service::promote(int inpipe) {
		if (_running.is_active()) {
			LOG_WARN("%s:promote(): action running: %s", _name.c_str(), _event->_name.c_str());
			return true; // defer action
		}

		if (is_disabled())
			return true;

		if (_state == Svc_Started) {
			if (_service_flags & HAS_PROMOTE) {
				transition(Svc_Promoting);
				if (!launch_command("promote", inpipe)) {
					LOG_WARN("Failed to execute %s%s", _path.c_str(), "/promote");
					return false;
				}
			}
			else {
				transition(Svc_Promoted);
			}
		}
		return true;
	}
	bool service_manager::service::demote(int inpipe) {
		if (_running.is_active()) {
			LOG_WARN("%s:demote(): action running: %s", _name.c_str(), _event->_name.c_str());
			return true; // defer action
		}
		if (_state == Svc_Promoted) {
			if (_service_flags & HAS_DEMOTE) {
				transition(Svc_Demoting);
				if (!launch_command("demote", inpipe)) {
					LOG_WARN("Failed to execute %s%s", _path.c_str(), "/demote");
					return false;
				}
			}
			else {
				transition(Svc_Started);
			}
		}
		return true;
	}

	bool service_manager::service::fail(int inpipe) {
		if (_running.is_active()) {
			LOG_WARN("%s:fail(): action running: %s", _name.c_str(), _event->_name.c_str());
		}
		if (_state > Svc_Failing) {
			//stop();
			if (_service_flags & HAS_FAIL) {
				transition(Svc_Failing);
				if (!launch_command("failed", inpipe)) {
					LOG_WARN("Failed to execute %s%s", _path.c_str(), "/failed");
					return false;
				}
			}
			else if (_state > Svc_Stopped) {
				return stop(inpipe);
			}
			else {
				transition(Svc_Failed);
			}
		}
		return true;
	}

	void service_manager::service::transition(ServiceState state) {
		if (_state != state) {
			LOG_INFO("%s[%s->%s]", _name.c_str(), service_state_string(_state), service_state_string(state));
			_state = state;
		}
	}

	bool service_manager::service::in_transition() const {
		switch (_state) {
		case Svc_Stopping:
		case Svc_Starting:
		case Svc_Demoting:
		case Svc_Promoting:
		case Svc_Failing:
			return true;
		default: break;
		};
		return false;
	}

	void service_manager::service::complete_transition() {
		switch (_state) {
		case Svc_Starting: transition(Svc_Started); break;
		case Svc_Stopping: transition(Svc_Stopped); break;
		case Svc_Promoting: transition(Svc_Promoted); break;
		case Svc_Demoting: transition(Svc_Started); break;
		case Svc_Failing: transition(Svc_Failed); break;
		default: break;
		}
	}

	ServiceState service_manager::service::closest(ServiceState state) const {
		if (!is_failed() && state == Svc_Failed)
			return Svc_Stopped;
		return state;
	}

	bool service_manager::resolves(ServiceState state, ServiceAction action) const {
		switch (action) {
		case Svc_Fail:
			return state == Svc_Failed;
		case Svc_Stop:
			return state == Svc_Failed ||
				state == Svc_Stopped;
		case Svc_Start:
			return state > Svc_Starting;
		case Svc_Demote:
			return state < Svc_Demoting;
		case Svc_Promote:
			return state == Svc_Promoted;
		default:
			break;
		}
		return false;
	}
}
