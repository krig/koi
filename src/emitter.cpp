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
#include "emitter.hpp"
#include "nexus.hpp"

#include <boost/bind.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

using namespace std;
using namespace boost;
using namespace boost::posix_time;
using namespace boost::uuids;

namespace koi {
	emitter::emitter(nexus& route, uint64_t tick_interval) :
		_nexus(route),
		_receivers(),
		_timer(route.io()),
		_last_tick(),
		_tick_interval(tick_interval),
		_current_tick(0),
		_active(false) {
		_timer.expires_at(posix_time::pos_infin);
	}

	emitter::~emitter() {
		_timer.cancel();
	}

	void emitter::add_receiver(const net::endpoint& ep) {
		_receivers.insert(ep);
	}

	void emitter::remove_receiver(const net::endpoint& ep) {
		_receivers.erase(ep);
	}

	void emitter::clear_receivers() {
		_receivers.clear();
	}

	void emitter::start() {
		_active = true;

		_timer.expires_from_now(posix_time::microseconds(_tick_interval));
		_timer.async_wait(bind(&emitter::_process_tick, this, _1));
	}

	void emitter::stop() {
		_active = false;
		_timer.cancel();
		_timer.expires_at(posix_time::pos_infin);
	}

	void emitter::update() {
		++_current_tick;
	}

	uint64_t emitter::uptime(ptime starttime) const {
		return (microsec_clock::universal_time() - starttime).total_milliseconds();
	}

	void emitter::immediate_tick() {
		if (!_receivers.empty()) {
			message m(_nexus.cfg()._uuid, _nexus.cfg()._cluster_id);
			_on_tick(&m);
			FOREACH(const net::endpoint& to, _receivers) {
				_nexus.send(m, to);
			}
		}
	}

	void emitter::_process_tick(const error_code& error) {
		if (error)
			return;

		if (_active) {
			immediate_tick();
			_last_tick = posix_time::microsec_clock::universal_time();
		}
		_timer.expires_from_now(posix_time::microseconds(_tick_interval));
		_timer.async_wait(bind(&emitter::_process_tick, this, _1));
	}
}
