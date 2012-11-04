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

#include <boost/date_time/posix_time/posix_time.hpp>

#include "emitter.hpp"
#include "servicemgr.hpp"
#include "msg.hpp"
#include "sequence.hpp"

namespace koi {
	using boost::posix_time::ptime;

	struct nexus;

	/*
	  Runner

	  A runner is a cluster node that runs services. It periodically
	  sends state information to the elector endpoint provided when
	  creating it.

	  The details of service management is handled by the
	  service_manager, and the details of sending and receiving
	  messages is handled by the node base class. The runner mostly
	  manages its own internal state.

	  As any node, you have to call init() once. After init() has been
	  called, start() can be called to start sending and receiving
	  packets.
	  When started, call update() repeatedly (while polling the
	  io_service passed to it) to receive and send over the network.
	*/
	struct runner {
		typedef std::vector<string> stringlist;
		typedef std::map<uuid, uint32_t> sequencenrs;

		struct elector_info {
			elector_info();
			uuid          _uuid;
			ptime         _last_seen;
			uuid          _master_uuid;
			uint64_t      _uptime;
			bool          _maintenance;
		};


		runner(nexus& route);
		~runner();

		bool init();

		void start();
		void stop();
		void update();

		bool settings_changed(const settings& newcfg, const settings& oldcfg);

		void handle(message& m);

		void on_tick(message* m);

		void transition(State new_state, const char* why);
		void switch_mode(RunnerMode new_mode, const char* why);

		void forget_failure();

		bool in_maintenance_mode() const;

		void rpc_start(msg::request* rq, msg::response::values& out);
		void rpc_stop(msg::request* rq, msg::response::values& out);
		void rpc_recover(msg::request* rq, msg::response::values& out);

		void _check_timeouts(const ptime& now);
		void _log_transition_state();
		void _check_elector_transition();
		void _update_elector_state(const ptime& now);
		void _check_recovery(const ptime& now);
		void _check_service_status();

		emitter         _emitter;
		elector_info    _elector;
		State           _state;
		RunnerMode      _mode;
		service_manager _services;
		sequence        _sequence;
		ptime           _last_transition;
		uint32_t        _failcount;
		bool            _warned_elector_lost;
		ptime           _starttime;
		bool            _enabled; // if false, go to <=Stopped
		ptime           _quorum_lost_time;
		bool            _quorum_lost;
	};
}
