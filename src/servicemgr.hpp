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
#pragma once

/*
 * TODO: OCF support?
 * services are order dependent:
 * name services NN-XXXX, like
 * /etc/koi/services/00-vip
 * /etc/koi/services/01-psm
 *
 * each folder can contain one of each of these commands
 * command is given as first parameter to service
 * start, stop, status, promote, demote, failed
 *
 * script should return 0 on success, 1 on failure
 * the scripts are executed as root with $KOI_SERVICE_HOME as homedir
 *
 * start is executed in order: 00, then 01... if one fails, the node is taken out of the cluster
 * stop is executed in reverse order: 01, then 00...
 * status is executed in parallell / order independent
 * promote is executed in order: 00, then 01...
 * demote is executed in reverse order: 01, then 00...
 */

#include "cmd.hpp"
#include "msg.hpp"
#include "service_info.hpp"

#include <set>

namespace koi {
	struct service_events {
		service_events() : _settings(0) {}
		service_events(const settings& s) : _settings(&s) {}
		const service_event* operator()(const char* n) const {
			return _settings->svc(n);
		}
		const settings* _settings;
	};

	struct service_manager {
		struct service {
			static const int NO_PRIORITY;

			service();
			service(service_events const& events, const char* name, const char* path);

			bool start(int inpipe);
			bool stop(int inpipe);
			bool status(int inpipe);
			bool promote(int inpipe);
			bool demote(int inpipe);
			bool fail(int inpipe);

			void transition(ServiceState state);
			bool in_transition() const;
			void complete_transition();

			bool launch_command(const char* c, int inpipe = -1);

			void unrun();
			void report_fail();
			void report_timeout();
			bool update_info(); // returns false if service is gone or broken
			ServiceState closest(ServiceState state) const;

			service_events _events;
			command      _running; // currently executing command
			const service_event* _event; // currently executing event
			string       _name; // echo if $KOISERVICES/00-echo/start is the start script
			string       _path; // full path to service
			int          _priority; // if script has a prio prefix, else NO_PRIO
			ServiceState _state;

			enum ServiceFlags {
				HAS_START     = 1,
				HAS_STOP      = 1<<1,
				HAS_STATUS    = 1<<2,
				HAS_PROMOTE   = 1<<3,
				HAS_DEMOTE    = 1<<4,
				HAS_FAIL      = 1<<5,
				SINGLE_SCRIPT = 1<<7,
				IS_FAILED     = 1<<8,
				IS_DISABLED   = 1<<9
			};

			inline bool is_failed() const { return _service_flags & IS_FAILED; }
			inline bool is_disabled() const { return _service_flags & IS_DISABLED; }
			inline bool single_script() const { return _service_flags & SINGLE_SCRIPT; }

			uint32_t _service_flags;

			static bool _read_service_flags(uint32_t& new_info, const char* path);
		};

		typedef std::map<string, service> services;


		void init(const char* servicesdir, const char* workingdir);
		~service_manager();

		bool update(const service_events& events, State state, uint64_t status_interval, uint64_t state_update_interval, bool maintenance_mode);

		void start();
		void stop();
		void promote();
		void demote();
		void fail();

		bool status(service_events const& events);

		// priority allow/disallow
		bool allow_start(const service& s) const;
		bool allow_stop(const service& s) const;
		bool allow_promote(const service& s) const;
		bool allow_demote(const service& s) const;

		void forget_failure();

		void report(msg::healthreport* hr);

		string to_string() const;

		string status_summary(const ptime& now, bool details=true) const;

		void _remove_services();
		void update_list(service_events const& events);
		bool verify_states(ptime now);
		bool update_states(uint64_t state_update_interval, bool force = false);
		bool _update_failed_service(service& s);
		bool _update_stopped_service(service& s);
		bool _update_started_service(service& s);
		bool _update_promoted_service(service& s);
		bool _update_service_state(service& s);
		bool _update_service(const ptime& now, service& s);

		void _service_failed(service& s);

		bool _is_masterslave_status(service& s) const;
		bool _check_masterslave_status(service& s) const;

		void wait_for_shutdown();
		void wait_for_demote(bool maintenance_mode);
		void wait_for_stop(bool maintenance_mode);
		bool complete_transition(ptime now, service& s);
		bool check_exitcode(service& s);
		void toggle_logproxy();

		bool resolves(ServiceState state, ServiceAction action) const;
		bool matches(ServiceState state, ServiceAction action) const;

		services                  _services;
		ServiceAction             _target_action;
		ptime                     _last_update_states;
		ptime                     _last_check;
		std::set<string>          _ignored_services;
		logging::logproxy         _logproxy;
		mutable std::stringstream _summary; // cache for status summary
	};
}
