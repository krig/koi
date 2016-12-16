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

namespace koi {
	struct archive;

	enum NodeFlags {
		NodeFlag_Elector = 0x1,
		NodeFlag_Runner = 0x2,
		NodeFlag_Leader = 0x4,
		NodeFlag_Failed = 0x8
	};

	const char* nodeflags_to_string(int flags);

	struct cluster {
		typedef boost::function<void (cluster*)> action;

		static const int Limit = 4;

		enum Mode {
			Servant = 0,
			Candidate = 1,
			Leader = 2
		};

		cluster(nexus& route, settings& cfg);
		~cluster();

		bool settings_changed(const settings& newcfg, const settings& oldcfg);

		void handle(message& m);
		void update();

		void update_leader();
		void update_candidate();
		void update_servant();
		void set_leader(const message& m);
		void update_state(const message& m);
		void reply_leader();
		void clear_callbacks();

		void set(NodeFlags flag);
		void clear(NodeFlags flag);
		int flags() const { return _flags; }

		void _update_state();

		net::endpoint get_elector() const;
		masterstate get_masterstate() const;
		void set_masterstate(const masterstate& ms);

		bool rpc_handle(msg::request* rq, msg::response::values& data);

		clusterstate   _state;
		network        _network;
		action         _on_up;
		action         _on_down;
		action         _on_state_change;
		settings&      _cfg;

		uuid           _leader;
		int64_t        _t;
		int64_t        _last_seen;
		int64_t        _candidate_time;
		int            _flags;
		Mode           _mode;
	};
}
