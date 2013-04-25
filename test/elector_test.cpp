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
#include "test.hpp"
#include "elector.hpp"
#include "nexus.hpp"
#include <boost/uuid/uuid_generators.hpp>

namespace koi {
	struct masterstate;
};

TEST_CASE("elector/promote", "test successful node promotion") {
	using namespace koi;
	using namespace std;
	using namespace boost;
	using namespace boost::posix_time;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char app[] = "koi";
	char* argv[1] = { app };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);

	net::io_service io_service;

	nexus ro(io_service, cfg);

	elector a(ro);
	ok = a.init();
	REQUIRE(ok);

	a.start();

	a.update();

	elector::runner_info r;
	ptime now = microsec_clock::universal_time();
	r._last_seen = now;
	r._last_failed = ptime(min_date_time);
	r._name = "test";
	r._uuid = uuids::random_generator()();
	r._uptime = 20000;
	r._state = S_Slave;
	r._maintenance = false;
	r._endpoints.insert(net::endpoint(net::ipaddr(), 6666));
	a._runners.insert(make_pair(r._uuid, r));

	ok = a.promote_node("test");
	REQUIRE(ok);

	REQUIRE(a._target_master == a._runners.begin());

	a.stop();
}

