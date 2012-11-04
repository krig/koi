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
#include "servicemgr.hpp"

TEST_CASE("servicemgr/init", "a very basic set of initial tests") {
	using namespace koi;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);

	service_manager sm;

	sm.init("../test/s1", "../test");

	ok = sm.update(service_events(cfg), S_Master, 0, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "svc");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	ok = sm.update(service_events(cfg), S_Master, 0, 0, false);
	REQUIRE(ok);

	REQUIRE(svc._state >= Svc_Starting);
}


TEST_CASE("servicemgr/fsm", "test sequence for the service state machine") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/s1", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "svc");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout = 2;
	while (timeout && (svc._state < Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	sm.promote();

	timeout = 8;
	while (timeout && (svc._state < Svc_Promoted)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Promoted);

	sm.demote();

	timeout = 8;
	while (timeout && (svc._state > Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	sm.stop();

	timeout = 8;
	while (timeout && (svc._state > Svc_Stopped)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Stopped);
}

TEST_CASE("servicemgr/failstart", "test failing service:start") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/failstart", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "failstart");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout = 20;
	while (timeout && (svc._state > Svc_Failed)) {
		// we expect ok to be false here
		sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Failed);
}

TEST_CASE("servicemgr/failstop", "test failing service:stop") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/failstop", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "failstop");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout;

	timeout = 20;
	while (timeout && (svc._state != Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	sm.stop();

	timeout = 20;
	while (timeout && (svc._state > Svc_Failed)) {
		// we expect ok to be false here
		sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Failed);
}

TEST_CASE("servicemgr/failstatus", "test failing service:status") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/failstatus", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "failstatus");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout;

	timeout = 20;
	while (timeout && (svc._state != Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	timeout = 20;
	while (timeout && (svc._state > Svc_Failed)) {
		// we expect ok to be false here
		sm.update(service_events(cfg), S_Master, 0, 0, false);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Failed);
}

TEST_CASE("servicemgr/failpromote", "test failing service:promote") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/failpromote", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "failpromote");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout;

	timeout = 20;
	while (timeout && (svc._state != Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	sm.promote();

	timeout = 20;
	while (timeout && (svc._state > Svc_Failed)) {
		// we expect ok to be false here
		sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Failed);
}

TEST_CASE("servicemgr/faildemote", "test failing service:demote") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/faildemote", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "faildemote");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout;

	timeout = 20;
	while (timeout && (svc._state != Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	sm.promote();

	timeout = 20;
	while (timeout && (svc._state != Svc_Promoted)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Promoted);


	sm.demote();

	timeout = 20;
	while (timeout && (svc._state > Svc_Failed)) {
		// we expect ok to be false here
		sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Failed);
}

TEST_CASE("servicemgr/failfailed", "test failing service:failed") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/failfailed", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "failfailed");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout;

	timeout = 20;
	while (timeout && (svc._state != Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	timeout = 20;
	while (timeout && (svc._state > Svc_Failed)) {
		// we expect ok to be false here
		sm.update(service_events(cfg), S_Master, 0, 0, false);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Failed);

	sm.stop();
	sm.wait_for_shutdown();
	REQUIRE(svc._state == Svc_Failed);
	REQUIRE(sm._target_action == Svc_Stop);
}

TEST_CASE("servicemgr/stopstatus", "test failing with status:stopped") {
	using namespace koi;

	int status_interval = 10*1000*1000;

	std::vector<std::string> configs;
	configs << "../test/test.conf";
	settings cfg;
	char tmp[] = "koi";
	char* argv[1] = { tmp };
	bool ok = cfg.boot(configs, false);
	REQUIRE(ok);
	service_manager sm;
	sm.init("../test/stopstatus", "../test");
	ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
	REQUIRE(ok);

	REQUIRE(sm._services.size() == 1);
	service_manager::service& svc = sm._services.begin()->second;
	REQUIRE(svc._name == "stopstatus");
	REQUIRE(svc._priority == service_manager::service::NO_PRIORITY);
	REQUIRE(svc._state < Svc_Starting);

	sm.start();

	timespec t = {0, 10*1000*1000};

	int timeout;

	timeout = 20;
	while (timeout && (svc._state != Svc_Started)) {
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	timeout = 20;
	while (timeout && (svc._state == Svc_Started)) {
		// we expect ok to be false here
		ok = sm.update(service_events(cfg), S_Master, 0, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}

	timeout = 20;
	while (timeout && (svc._state != Svc_Started)) {
		// we expect ok to be false here
		ok = sm.update(service_events(cfg), S_Master, status_interval, 0, false);
		REQUIRE(ok);
		nanosleep(&t, 0);
		timeout--;
	}
	REQUIRE(svc._state == Svc_Started);

	sm.stop();
	sm.wait_for_shutdown();
	REQUIRE(sm._target_action == Svc_Stop);
}
