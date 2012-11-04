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
#include "msg.hpp"

#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>

using namespace koi;
using namespace koi::msg;

boost::uuids::uuid test_uuid = boost::uuids::random_generator()();

namespace koi {
	extern bool debug_mode;
}

TEST_CASE("messages/hrencode", "tests for encoding a health report") {
	message m;
	healthreport* hr = m.set_body<healthreport>();
	m._sender_uuid = test_uuid;
	hr->_name = "test";
	hr->_uptime = 0;
	hr->_state = S_Master;
	hr->_mode = R_Active;
	hr->_maintenance = false;
	hr->_service_action = Svc_Promote;

	hr->_services.push_back(service_info("psm", "", Svc_Started, false));
	hr->_services.push_back(service_info("vip", "", Svc_Promoted, false));
	hr->_services.push_back(service_info("dbcleaner", "", Svc_Promoted, false));

	std::vector<uint8_t> to;
	REQUIRE(encode(to, &m, "testpass") == true);
	LOG_TRACE("herencode: encoded length: %d\n", (int)to.size());
}

TEST_CASE("messages/hrpingpong", "tests encoding, then decoding a health report") {
	debug_mode = true;
	message m;
	healthreport* hr = m.set_body<healthreport>();
	m._sender_uuid = test_uuid;
	hr->_name = "mr. test";
	hr->_uptime = 100;
	hr->_state = S_Elector;
	hr->_mode = R_Active;
	hr->_maintenance = true;
	hr->_service_action = Svc_Stop;

	hr->_services.push_back(service_info("psm", "", Svc_Stopped, false));
	hr->_services.push_back(service_info("vip", "", Svc_Started, false));
	hr->_services.push_back(service_info("dbcleaner", "", Svc_Started, false));

	std::vector<uint8_t> to;
	bool enc = encode(to, &m, "testpass");
	REQUIRE(enc);
	LOG_TRACE("hrpingpong: encoded length: %d\n", (int)to.size());

	message out;
	bool dec = decode(&out, to, "testpass");
	REQUIRE(dec);
	REQUIRE(out._sender_uuid == test_uuid);
	REQUIRE(out._op == base::HealthReport);

	LOG_TRACE("hrpingpong succeeded\n");
}

TEST_CASE("messages/wrong", "tests encoding, then decoding with bad pass") {
	debug_mode = true;
	message m;
	healthreport* hr = m.set_body<healthreport>();
	m._sender_uuid = test_uuid;
	hr->_name = "mr. test";
	hr->_uptime = 100;
	hr->_state = S_Elector;
	hr->_mode = R_Active;
	hr->_maintenance = true;
	hr->_service_action = Svc_Start;

	hr->_services.push_back(service_info("psm", "", Svc_Started, false));
	hr->_services.push_back(service_info("vip", "", Svc_Promoting, false));
	hr->_services.push_back(service_info("dbcleaner", "", Svc_Promoting, false));

	std::vector<uint8_t> to;
	bool enc = encode(to, &m, "testpass");
	REQUIRE(enc);
	LOG_TRACE("hrpingpong: encoded length: %d\n", (int)to.size());

	message out;
	bool dec = decode(&out, to, "testpiss");
	REQUIRE(!dec);
}

TEST_CASE("messages/emptybuffer", "see if we can decode from an empty buffer") {
	message m;
	response* r = m.set_body<response>();
	r->_response.insert(std::make_pair("testkey", "testvalue"));
	m._sender_uuid = test_uuid;

	std::vector<uint8_t> to;
	bool enc = encode(to, &m, "testpass");
	REQUIRE(enc == true);

	to.clear();
	bool dec = decode(&m, to, "testpass");
	REQUIRE(dec == false);
}

TEST_CASE("messages/partial", "try to decode partial message") {
	message m;
	response* r = m.set_body<response>();
	r->_response.insert(std::make_pair("testkey", "testvalue"));
	m._sender_uuid = test_uuid;

	std::vector<uint8_t> to;
	bool enc = encode(to, &m, "testpass");
	REQUIRE(enc == true);

	std::vector<uint8_t> partial;

	m._sender_uuid = boost::uuids::nil_uuid();

	partial = to;
	bool dec = decode(&m, partial, "testpass");
	REQUIRE(dec == true);

	REQUIRE(m._sender_uuid == test_uuid);

	partial = to;
	partial.resize(((to.size()*4/5)/4+1)*4);
	dec = decode(&m, partial, "testpass");
	REQUIRE(dec == false);

	partial = to;
	partial.resize(((to.size()*3/4)/4+1)*4);
	dec = decode(&m, partial, "testpass");
	REQUIRE(dec == false);

	partial = to;
	partial.resize(((to.size()/2)/4+1)*4);
	dec = decode(&m, partial, "testpass");
	REQUIRE(dec == false);
}

namespace {
	string randstr(int len) {
		string tmp(len, 0);
		for (int i = 0; i < len-1; ++i) {
			tmp[i] = 'a'+rand()%3;
		}
		return tmp;
	}
}

TEST_CASE("messages/compression", "try to decode compressed message") {
	message m;
	response* r = m.set_body<response>();

	srand(1292);
	for (int i = 0; i < 100; ++i)
		r->_response.insert(std::make_pair(randstr(10), randstr(20)));

	m._sender_uuid = test_uuid;

	std::vector<uint8_t> to;
	bool enc = encode(to, &m, "testpass");
	REQUIRE(enc == true);
	m._sender_uuid = boost::uuids::nil_uuid();

	bool dec = decode(&m, to, "testpass");
	REQUIRE(dec == true);

	r = m.body<response>();
	LOG_TRACE("n: %d\n", (int)r->_response.size());
	LOG_TRACE("a: %s => %s\n", r->_response.begin()->first.c_str(), get<string>(r->_response.begin()->second).c_str());
}

TEST_CASE("messages/ipencoding", "try to put/remote ip addresses from archives") {
	archive a;
	a << parse_endpoint("127.0.0.1:100", 0);
	a << parse_endpoint("2001:0db8:85a3:0000:0000:8a2e:0370:7334", 17);
	a.done();
	REQUIRE(a.valid());
	reader r(a);
	net::endpoint ep1, ep2;
	r >> ep1 >> ep2;
	net::endpoint v6 = parse_endpoint("2001:0db8:85a3:0000:0000:8a2e:0370:7334", 17);
	REQUIRE(ep1.address().to_string() == "127.0.0.1");
	REQUIRE(ep1.port() == 100);
	REQUIRE(ep2 == v6);
	REQUIRE(ep2.port() == 17);

	LOG_TRACE("archive size: %d\n", (int)a.size());
}
