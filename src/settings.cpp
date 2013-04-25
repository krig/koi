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

#include "file.hpp"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include "stringutil.hpp"

using namespace std;
using namespace boost;

namespace {
	template <typename T>
	T clamp(T val, const T& lo, const T& hi) {
		if (val < lo)
			val = lo;
		else if (val > hi)
			val = hi;
		return val;
	}
}

namespace koi {
	bool debug_mode = false;

	net::endpoint parse_endpoint(const char* str, uint16_t default_port) {
		boost::system::error_code err;
		uint16_t port = default_port;
		net::ipaddr ip;
		try {
			regex v4matcher("^[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]{1,3}\\.[0-9]+\\:[0-9]{1,5}$");
			regex v6matcher("^\\[[0-9a-f:]+\\]\\:[0-9]{1,5}$");

			if (regex_match(str, v4matcher)) {
				const char* portsplit = strrchr(str, ':');
				if (portsplit == 0) {
					LOG_ERROR("Error in regex: %s matched", str);
					return net::endpoint();
				}
				string sport(portsplit+1);
				string ipa(str, portsplit);
				port = lexical_cast<uint16_t>(sport);
				ip = net::ipaddr::from_string(ipa, err);
			} else if (regex_match(str, v6matcher)) {
				const char* portsplit = strrchr(str, ':');
				if (portsplit == 0) {
					LOG_ERROR("Error in regex: %s matched", str);
				}
				string sport(portsplit+1);
				string ipa(str+1, portsplit-1);
				port = lexical_cast<uint16_t>(sport);
				ip = net::ipaddr::from_string(ipa, err);
			}
			else {
				ip = net::ipaddr::from_string(str, err);
			}
		}
		catch(bad_lexical_cast& e) {
			LOG_TRACE("Caught bad lexical cast: %s", e.what());
			return net::endpoint();
		}
		if (err) {
			return net::endpoint();
		}
		else {
			return net::endpoint(ip, port);
		}
	}

	string to_string(const net::endpoint& ep) {
		stringstream ss;
		ss << ep;
		return ss.str();
	}

	std::ostream & operator<<(std::ostream & o, const net::endpoint& ep) {
		const net::ipaddr& addr = ep.address();
		if (addr.is_v6()) {
			o << "[" << addr << "]:";
		}
		else {
			o << addr << ":";
		}
		o << ep.port();
		return o;
	}

	const char* state_to_string(State s) {
		static const char* state_trans[] = {
			"Failed",
			"Disconnected",
			"Stopped", // not running/not voting
			"Offline", // running offline/not voting
			"Slave", // running slave/not voting
			"Master",// running master/not voting
			"Elector", // voting but not executing services
			"Other" // non-elector, non-voting cluster member
		};
		if (s >= 0 && s < (int)ASIZE(state_trans))
			return state_trans[s];
		return "Unknown";
	}

	const char* mode_to_string(RunnerMode m) {
		static const char* mode_trans[] = {
			"Passive",
			"Active"
		};
		if (m >= 0 && m < (int)ASIZE(mode_trans))
			return mode_trans[m];
		return "Unknown";
	}

	void readtime(const boost::property_tree::ptree& tr, uint64_t& t, const char* s) {
		string val = tr.get<string>(s, lexical_cast<string>((uint32_t)(t/units::milli))+"ms");
		if (endswith(val, "ms")) {
			t = lexical_cast<uint64_t>(val.substr(0, val.length()-2))*units::micro/units::milli;
		}
		else if (endswith(val, "m")) {
			t = lexical_cast<uint64_t>(val.substr(0, val.length()-1))*60*units::micro;
		}
		else if (endswith(val, "h")) {
			t = lexical_cast<uint64_t>(val.substr(0, val.length()-1))*60*60*units::micro;
		}
		else if (endswith(val, "s")) {
			t = lexical_cast<uint64_t>(val.substr(0, val.length()-1))*units::micro;
		}
		else { // default is ms
			t = lexical_cast<uint64_t>(val)*units::micro/units::milli;
		}
	}

	settings::settings() :
		_starttime(),
		_uuid(uuids::nil_uuid()),
		_name(),
		_elector(true),
		_runner(false),
		_force_restart(false),
		_port(KOI_DEFAULT_CLUSTER_PORT),
		_cluster_maintenance(false),
		_boot_count(0),

		_reuse_address(true),
		_incremental_port(false),

		_loglevel(logging::Trace),
		_cluster_id(13),
		_cluster_quorum(0),

		_pass("secret"),
		_transport(),

		_services_folder(KOI_CONFIG_SERVICES),
		_services_workingdir(KOI_CONFIG_SERVICES),

		// time units are in microseconds
		_on_none("none", 1*units::micro),
		_on_start("start", uint64_t(360)*units::micro),
		     _on_stop("stop", uint64_t(360)*units::micro),
		     _on_status("status", uint64_t(120)*units::micro),
		     _on_promote("promote", 30*units::micro),
		     _on_demote("demote", 30*units::micro),
		     _on_failed("failed", 20*units::micro),

		     _status_interval(10*units::micro),
		     _cluster_update_interval(units::micro),
		     _state_update_interval(units::micro/3),
		     _runner_tick_interval(1*units::micro),
		     _elector_tick_interval(1*units::micro),
		     _runner_elector_lost_time(3*units::micro),
		     _runner_elector_gone_time(60*8*units::micro), //8 minutes
		     _quorum_demote_time(10*units::micro), // 10 seconds
		     _mainloop_sleep_time(units::micro/3),
		     _master_dead_time(5*units::micro),
		     _elector_startup_tolerance(5*units::micro),
		     _elector_initial_promotion_delay(5*units::micro),
		     _service_auto_recover(120),
		     _auto_recover_time(10*units::micro),
		     _auto_recover_wait_factor(1),
		     _service_failcount_reset_time(60L*30L*units::micro),
		     _runner_failure_promotion_timeout(60L*units::micro) {
	}

	service_event::service_event(const char* name, uint64_t t) : _name(name), _timeout(t) {
	}

	const service_event* settings::svc(const char* name) const {
		/*
		  if (strcmp(name, "none") == 0) {
		  return &_on_none;
		  }
		  else if (strcmp(name, "start") == 0) {
		  return &_on_start;
		  }
		  else if (strcmp(name, "stop") == 0) {
		  return &_on_stop;
		  }
		  else if (strcmp(name, "status") == 0) {
		  return &_on_status;
		  }
		  else if (strcmp(name, "promote") == 0) {
		  return &_on_promote;
		  }
		  else if (strcmp(name, "demote") == 0) {
		  return &_on_demote;
		  }
		  else if (strcmp(name, "failed") == 0) {
		  return &_on_failed;
		  }
		  else {
		  return &_on_none;
		  }
		*/
		switch (name[3]) {
		case 'e': return &_on_none;
		case 'r': return &_on_start;
		case 'p': return &_on_stop;
		case 't': return &_on_status;
		case 'm': return &_on_promote;
		case 'o': return &_on_demote;
		case 'l': return &_on_failed;
		default: break;
		}
		return &_on_none;
	}

	bool settings::read_config(const vector<string>& configs, bool verbose) {
		using namespace property_tree;
		ptree pt;

		try {
			string cfgerr = "";
			bool cfgfound = false;
			for (size_t i = 0; i < configs.size(); ++i) {
				try {
					info_parser::read_info(configs[i], pt);
					cfgfound=true;
				}
				catch (const ptree_error& e) {
					if (cfgerr.length())
						cfgerr += ", ";
					cfgerr += e.what();
				}
			}
			if (!cfgfound && verbose) {
				LOG_TRACE("No config file found: %s", cfgerr.c_str());
			}

			boost::system::error_code err;

			_elector = pt.get<bool>("node.elector", _elector);
			_runner = pt.get<bool>("node.runner", _runner);
			_force_restart = pt.get<bool>("node.force_restart", _force_restart);

			_port = pt.get<uint16_t>("node.port", _port);

			_cluster_maintenance = pt.get<bool>("node.maintenance", _cluster_maintenance);

			_reuse_address = pt.get<bool>("node.reuse_address", _reuse_address);
			_incremental_port = pt.get<bool>("node.increment_port", _incremental_port);

			{
				const char* loglevels[] = {"trace", "info", "warn", "error"};
				string loglevel_name = pt.get<string>("node.loglevel", loglevels[logging::Trace]);
				transform(loglevel_name.begin(), loglevel_name.end(), loglevel_name.begin(), ::tolower);
				for (size_t i = 0; i < ASIZE(loglevels); ++i)
					if (loglevel_name == loglevels[i])
						_loglevel = (LogLevel)i;
			}

			_cluster_id = pt.get<int32_t>("cluster.id", _cluster_id);
			_cluster_quorum = pt.get<int32_t>("cluster.quorum", _cluster_quorum);

			readtime(pt, _on_start._timeout, "service.start_timeout");
			readtime(pt, _on_stop._timeout, "service.stop_timeout");
			readtime(pt, _on_status._timeout, "service.status_timeout");
			readtime(pt, _on_promote._timeout, "service.promote_timeout");
			readtime(pt, _on_demote._timeout, "service.demote_timeout");
			_service_auto_recover = pt.get<uint32_t>("service.auto_recover", (uint32_t)_service_auto_recover);
			_auto_recover_wait_factor = pt.get<int>("service.auto_recover_wait_factor", _auto_recover_wait_factor);
			if (_auto_recover_wait_factor < 1 || _auto_recover_wait_factor > AUTO_RECOVER_MAX_FACTOR) {
				LOG_WARN("Auto recover wait factor clamped %d <= f <= %d",
				         1, AUTO_RECOVER_MAX_FACTOR);
				_auto_recover_wait_factor = clamp(_auto_recover_wait_factor,
				                                  1, AUTO_RECOVER_MAX_FACTOR);
			}

			_pass = pt.get<string>("cluster.password", _pass);
			_transport = pt.get<string>("cluster.transport", _transport);

			_services_folder = pt.get<string>("service.folder", _services_folder);
			_services_workingdir = pt.get<string>("service.workingdir", _services_workingdir);

			readtime(pt, _status_interval, "time.status_interval");
			readtime(pt, _cluster_update_interval, "time.cluster_update_interval");
			readtime(pt, _state_update_interval, "time.state_update_interval");
			readtime(pt, _elector_tick_interval, "time.elector_tick_interval");
			readtime(pt, _runner_tick_interval, "time.runner_tick_interval");
			readtime(pt, _runner_elector_lost_time, "time.elector_lost_time");
			readtime(pt, _runner_elector_gone_time, "time.elector_gone_time");
			readtime(pt, _quorum_demote_time, "time.quorum_demote_time");
			readtime(pt, _mainloop_sleep_time, "time.mainloop_sleep_time");
			readtime(pt, _master_dead_time, "time.master_dead_time");
			readtime(pt, _elector_startup_tolerance, "time.elector_startup_tolerance");
			readtime(pt, _elector_initial_promotion_delay, "time.initial_promotion_delay");

			readtime(pt, _auto_recover_time, "time.auto_recover_time");
			readtime(pt, _service_failcount_reset_time, "time.failcount_reset");
			readtime(pt, _runner_failure_promotion_timeout, "time.failure_promotion_timeout");

			logging::set_log_level(_loglevel);
		}
		catch (const ptree_error& e) {
			LOG_ERROR("Configuration error: %s", e.what());
			return false;
		}

		return true;
	}


	bool settings::boot(vector<string> const& configs, bool verbose) {
		// load initial config
		// setup basic environment
		// verify machine
		if (_starttime == posix_time::ptime(posix_time::not_a_date_time)) {
			_starttime = posix_time::microsec_clock::universal_time();
		}

		_uuid = boost::uuids::random_generator()();
		if (_name == "")
			_name = asio::ip::host_name();

		if (configs.size() && read_config(configs, verbose)) {
			if (verbose) {
				LOG_INFO("Name: %s", _name.c_str());
				stringstream suid;
				suid << _uuid;
				LOG_INFO("UUID: %s", suid.str().c_str());
			}

			if (verbose) {
				LOG_INFO("Local: 0.0.0.0:%d", (int)_port);
				LOG_INFO("Elector: %s", _elector ? "Yes" : "No");
				LOG_INFO("Runner: %s", _runner ? "Yes" : "No");
			}
			return true;
		}
		else {
			LOG_ERROR("Failed to read config, terminating.");
		}
		return false;
	}
}
