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

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/info_parser.hpp>

using namespace boost;
using namespace boost::property_tree;

namespace koi {
	net::endpoint parse_endpoint(const char* str, uint16_t port);
	void readtime(const ptree& tr, uint64_t& t, const char* s);
}

using namespace koi;

TEST_CASE("boot/configuration", "basic test for the config file reading") {
	std::vector<std::string> configs;
	configs << "../test/test.conf" << "./test/test.conf";
	settings cfg;
	bool ok = cfg.read_config(configs);
	REQUIRE(ok);
	//REQUIRE(cfg._ip.to_string() == "127.0.0.1");
	REQUIRE(cfg._port == 1234);
	REQUIRE(cfg._runner == false);
	REQUIRE(cfg._cluster_maintenance == true);
	REQUIRE(cfg._on_start._timeout == 90*1e6);
	REQUIRE(cfg._on_stop._timeout == 360*1e6);
	REQUIRE(cfg._on_status._timeout == 19*1e6);
	REQUIRE(cfg._on_promote._timeout == 60*1e6);
	REQUIRE(cfg._on_demote._timeout == 120*60*(uint64_t)1e6);
	REQUIRE(cfg._pass == "testpass");
	REQUIRE(cfg._status_interval == 1*1e5);
}

TEST_CASE("boot/endpoint_parser", "parses ip:port pairs for both v4 and v6 addresses") {

	const uint16_t dp = KOI_DEFAULT_CLUSTER_PORT;

	net::endpoint fail;
	REQUIRE(parse_endpoint("192.168.1.1:100", dp) != fail);
	REQUIRE(parse_endpoint("[fe80:0:0:0:202:b3ff:fe1e:832]:100", dp) != fail);
	REQUIRE(parse_endpoint("192.168.1.1", 12929) == net::endpoint(net::ipaddr::from_string("192.168.1.1"), 12929));
	REQUIRE(parse_endpoint("fe80:0:0:0:202:b3ff:fe1e:832", 1345) == net::endpoint(net::ipaddr::from_string("fe80:0:0:0:202:b3ff:fe1e:832"), 1345));
	REQUIRE(parse_endpoint("ninjas:100", dp) == fail);
	REQUIRE(parse_endpoint("[a:a:a:a:a:a:a:a:a:a:a:a:a:a:a:a:a:a:a]:80", dp) == fail);
}

TEST_CASE("boot/readtime", "test for time suffix parsing") {
	ptree pt;
	uint64_t t;
	std::stringstream ss(
	                     "a 3000\n"
	                     "b 3000ms\n"
	                     "c 3s\n"
	                     "d 3m\n"
	                     "e 3h\n");
	info_parser::read_info(ss, pt);
	readtime(pt, t, "a");
	REQUIRE(t == (uint64_t)3000*units::milli);
	readtime(pt, t, "b");
	REQUIRE(t == (uint64_t)3000*units::milli);
	readtime(pt, t, "c");
	REQUIRE(t == (uint64_t)3*units::micro);
	readtime(pt, t, "d");
	REQUIRE(t == (uint64_t)3*60*units::micro);
	readtime(pt, t, "e");
	REQUIRE(t == (uint64_t)3*60*60*units::micro);
}
