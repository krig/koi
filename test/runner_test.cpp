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
#include "runner.hpp"
#include "nexus.hpp"

using namespace koi;
using namespace std;
using namespace boost;
using namespace boost::posix_time;

namespace {
	struct transition {
		State from;
		State to;
	};

	void test_transition(const char* tname, runner& r, transition* tbl, size_t len) {
		for (size_t i = 0; i < len; ++i) {
			r._state = tbl[i].from;
			r._check_elector_transition();
			if (r._state != tbl[i].to) {
				printf("%s: from %s expected %s, was %s\n",
				          tname,
				          state_to_string(tbl[i].from),
				          state_to_string(tbl[i].to),
				          state_to_string(r._state));
			}
			REQUIRE(r._state == tbl[i].to);
		}
	}
}

TEST_CASE("runner/elector_transition", "Verify elector induced state transitions") {
	vector<string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	net::io_service io_service;
	nexus nex(io_service, cfg);
	runner r(nex);
	ok = r.init();
	REQUIRE(ok);
	r.start();

	transition master_transitions[] = {
		{ S_Failed, S_Failed },
		{ S_Disconnected, S_Live },
		{ S_Stopped, S_Live },
		{ S_Live, S_Master },
		{ S_Slave, S_Master },
		{ S_Master, S_Master }
	};

	transition nonmaster_transitions[] = {
		{ S_Failed, S_Failed },
		{ S_Disconnected, S_Live },
		{ S_Stopped, S_Live },
		{ S_Live, S_Live },
		{ S_Slave, S_Slave },
		{ S_Master, S_Slave }
	};

	transition disabled_transitions[] = {
		{ S_Failed, S_Failed },
		{ S_Disconnected, S_Stopped },
		{ S_Stopped, S_Stopped },
		{ S_Live, S_Stopped },
		{ S_Slave, S_Stopped },
		{ S_Master, S_Stopped }
	};

	r._enabled = true;

	r._elector._master_uuid = cfg._uuid;
	test_transition("master", r, master_transitions, ASIZE(master_transitions));

	r._elector._master_uuid = uuid();
	test_transition("nonmaster", r, nonmaster_transitions, ASIZE(nonmaster_transitions));

	r._enabled = false;

	r._elector._master_uuid = cfg._uuid;
	test_transition("disabled-1", r, disabled_transitions, ASIZE(disabled_transitions));
	r._elector._master_uuid = uuid();
	test_transition("disabled-2", r, disabled_transitions, ASIZE(disabled_transitions));

	r.stop();
}

TEST_CASE("runner/forget_failure", "forget_failure should move state from Failed to Disconnected") {
	vector<string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	net::io_service io_service;
	nexus nex(io_service, cfg);
	runner r(nex);
	ok = r.init();
	REQUIRE(ok);
	r.start();

	r._state = S_Failed;
	r.forget_failure();
	REQUIRE(r._state == S_Disconnected);
}

TEST_CASE("runner/check_timeouts", "Test timeout state transitions") {
	vector<string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	cfg._runner_elector_gone_time = 100*units::milli;
	cfg._runner_elector_lost_time = 50*units::milli;
	net::io_service io_service;
	nexus nex(io_service, cfg);
	runner r(nex);
	ok = r.init();
	REQUIRE(ok);
	r.start();

	ptime t0 = microsec_clock::universal_time();
	ptime t1 = t0 + milliseconds(10);
	ptime t2 = t0 + milliseconds(70);
	ptime t3 = t0 + milliseconds(150);

	r._elector._last_seen = t0;
	r._warned_elector_lost = false;
	r._state = S_Master;
	r._check_timeouts(t1);
	REQUIRE(r._warned_elector_lost == false);
	REQUIRE(r._state == S_Master);

	r._check_timeouts(t2);
	REQUIRE(r._warned_elector_lost == true);
	REQUIRE(r._state == S_Master);

	r._check_timeouts(t3);
	REQUIRE(r._warned_elector_lost == true);
	REQUIRE(r._state == S_Slave);

}
