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
#include <set>
#include <boost/asio/deadline_timer.hpp>

#include "net.hpp"
#include "msg.hpp"

namespace koi {
	using boost::system::error_code;
	using boost::asio::deadline_timer;

	struct nexus;

	/*
	 * Emitter is a base class for Electors and Runners.
	 *
	 * Maintains a list of receivers.
	 * Periodically a callback is executed to collect
	 * data (on_tick) which is then sent to each receiver.
	 *
	 */
	struct emitter {
		// called when we are about to tick, should create the message to send
		typedef boost::function<void(message*)> on_tick_callback;

		typedef std::set<net::endpoint> endpoints;

		emitter(nexus& route, uint64_t tick_interval);
		~emitter();

		void add_receiver(const net::endpoint& ep);
		void remove_receiver(const net::endpoint& ep);
		void clear_receivers();

		void start();
		void stop();
		void update();


		uint64_t uptime(ptime starttime) const;

		void immediate_tick();

		void _process_tick(const error_code& error);

		nexus&                      _nexus;
		endpoints                   _receivers;
		deadline_timer              _timer;
		on_tick_callback            _on_tick;
		ptime                       _last_tick; // last time we sent a tick
		uint64_t                    _tick_interval; // interval with which we send, in microseconds
		uint32_t                    _current_tick;
		bool                        _active;
	};

}

