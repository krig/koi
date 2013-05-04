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

#include <map>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "emitter.hpp"
#include "msg.hpp"
#include "net.hpp"
#include "service_info.hpp"
#include "mru.hpp"

namespace koi {
	/*
	  Elector

	  The elector is the decision making node in the cluster. It
	  doesn't manage services, but elects and manages masterness.

	  As any node, you have to call init() once. After init() has been
	  called, start() can be called to start sending and receiving
	  packets.
	  When started, call update() repeatedly (while polling the
	  io_service passed to it) to receive and send over the network.

	  The elector keeps track of runners it has seen recently and
	  sends status updates at a steady pace to them. The most
	  important aspect of the status update is telling runners which
	  node is the master. Nodes are identified by UUIDs, unique in a
	  running cluster.

	  In a live cluster there is ever only one elector.

	  runner_info holds all the knowledge the elector has about each
	  runner. _runners is a map from endpoint -> runner_info.

	  member functions prefixed with _ are partial update functions,
	  called by update().

	*/

	struct masterstate;

	struct elector {
		struct runner_info {
			typedef std::vector<service_info> servicelist;
			typedef mru<net::endpoint> endpoints;

			runner_info();

			ptime         _last_seen;
			ptime         _last_failed;
			string        _name;
			uuid          _uuid;
			uint64_t      _uptime; // uptime in milliseconds
			State         _state;
			RunnerMode    _mode;
			bool          _maintenance;
			ServiceAction _service_action;
			endpoints     _endpoints;

			servicelist _services;

			bool alive(uint64_t master_dead_time, const ptime& now) const;
			bool electable(const ptime& now, uint64_t promotion_timeout) const;

			bool promoted_service() const;
			bool failed_service() const;

			void read(const masterstate& state);
		};

		typedef std::map<uuid, runner_info> runners;

		struct failure_info {
			ptime   _time;
			string  _name;
			uuid    _uuid;

			failure_info() {}
			failure_info(ptime t, const string& name, const uuid& id) : _time(t), _name(name), _uuid(id) {}
			string to_string() const;
		};

		typedef std::vector<failure_info> failures;

		elector(nexus& route);
		~elector();

		bool init(const ptime& starttime);
		void start();
		void stop();
		void update();
		bool settings_changed(const settings& newcfg, const settings& oldcfg);

		void handle(message& m);

		void on_tick(message* m);

		bool _repromote_master();
		bool _check_runner_health(const ptime& now, int& npromoted, int& nfailed);
		bool _check_master_health();
		bool _should_skip_election(int npromoted);
		bool _elect_target_master();
		bool _elect_candidate(runners::iterator runner);
		bool _find_candidates(ptime now, std::vector<runners::iterator>& candidates);
		bool _election(ptime now, int npromoted);
		bool _forget_old_runners(const ptime& now);
		void transition_runner(runners::iterator i, State newstate);
		bool promote_node(const char* name);
		bool demote_master();
		bool elect_node(runners::iterator const& i);
		runners::iterator find_runner(const char* search_by);


		void rpc_status(msg::request* rq,
		                msg::response::values& response);
		void rpc_promote(msg::request* rq,
		                 msg::response::values& response);
		void rpc_demote(msg::request* rq, msg::response::values& response);
		void rpc_elect(msg::request* rq, msg::response::values& response);
		void rpc_failures(msg::request* rq, msg::response::values& response);
		void rpc_maintenance(msg::request* rq, msg::response::values& response);

		void save_state();
		void load_state();

		void register_failure(const runner_info& runner);

		emitter           _emitter;
		failures          _failures;
		runners           _runners;
		runners::iterator _master; // this node IS master
		runners::iterator _target_master; //this node should be master
		bool              _manual_master_mode; // the master has been demoted; manual promotion is required
		ptime             _starttime;
		ptime             _leadertime;
		ptime             _last_state_save;
		uint32_t          _state_sum;
	};

}
