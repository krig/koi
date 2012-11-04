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
#include "archive.hpp"
#include "hex.hpp"
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>


using namespace koi;

TEST_CASE("chive/basic", "just testing some basic serialization/deserialization") {
	std::vector<std::string> slist;
	slist.push_back("one");
	slist.push_back("tu");
	archive a;
	a << 1 << "hello" << "wee" << slist;
	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());


	REQUIRE(a.size() == 23);

	archive::iterator i = a.begin();
	archive::iterator e = a.end();
	REQUIRE(i != e);
	archive::iterator z = ++i;
	REQUIRE(z != i);
	REQUIRE(z != e);

	REQUIRE(a.begin().type() == SmallInt);
	REQUIRE(a.begin().size() == 0);

	REQUIRE(a.end()._pos == a.end()._end);
	REQUIRE(a.end().is_end());

	i = a.begin();
	e = a.end();
	int brk = 4;
	while (brk) {
		++i;
		--brk;
	}
	REQUIRE(i == e);
}

TEST_CASE("chive/list", "testing that we can write and iterate around lists") {
	archive a;

	std::vector<int> l;
	l.push_back(1);
	l.push_back(1e7);

	a << l;

	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());

	archive::iterator i = a.begin();
	archive::iterator e = a.end();

	archive::iterator ipp = i++;
	i = a.begin();
	archive::iterator ppi = ++i;
	i = a.begin();
	REQUIRE(ppi != i);
	REQUIRE(ipp == i);
	REQUIRE(ipp != ppi);
	REQUIRE(ppi == e);

	archive::iterator lst = a.begin();
	int brk = 3;
	for (archive::iterator k = lst.begin(); brk && (k != lst.end()); ++k, --brk) {
		LOG_TRACE("%s: size: %d, value: %d\n", k.type_name(), (int)k.size(), k.get<int>());
	}
	REQUIRE(brk != 0);
}

TEST_CASE("chive/uuids", "testing that we write/read uuids correctly") {
	using namespace boost::uuids;

	archive a;

	a << nil_uuid();

	a << random_generator()();

	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());

	REQUIRE(a.size() == (internal::overhead*3 + 2 + 16));

	uuid x = a.begin().get<uuid>();
	REQUIRE(x == nil_uuid());

	uuid id = random_generator()();

	a.clear();
	a << id;
	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());

	uuid y = a.begin().get<uuid>();
	REQUIRE(y == id);
}

TEST_CASE("chive/posix_time", "testing that we write/read posix times correctly") {
	using namespace boost::posix_time;

	archive a;

	std::string ts("2090-01-20 23:59:59.035");
	ptime t1(time_from_string(ts));

	LOG_TRACE("%zu\n", sizeof(t1));

	a << t1;

	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());

	REQUIRE(a.size() == (internal::overhead*3 + 1 + sizeof(uint64_t)));

	ptime t2 = a.begin().get<ptime>();

	REQUIRE((t2 - t1).total_milliseconds() == 0);
}

TEST_CASE("chive/string", "testing that we write/read strings correctly") {
	archive a;

	a << "hello";
	a.done();

	REQUIRE(a.size() == (internal::overhead*3 + 1 + 5));
	REQUIRE(a.begin().type() == SmallString);

	a.clear();

	const char* longs = "This string exceeds the 15 character limit of small strings";
	a << longs;
	a.done();

	REQUIRE(a.size() == (int)(internal::overhead*3 + 2 + strlen(longs)));
	REQUIRE(a.begin().type() == String);

	a.clear();

	a << "";
	a.done();

	REQUIRE(a.size() == internal::overhead*3 + 1);
	REQUIRE(a.begin().type() == SmallString);
}

TEST_CASE("chive/rawdata", "testing that we write/read raw data correctly") {
	archive a;

	char raw[4] = "abc";

	a.append((const uint8_t*)raw, 4);
	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());

	REQUIRE(a.size() == (internal::overhead*3 + 2 + 4));
}

TEST_CASE("chive/numbers", "testing different numbers") {

	archive a;

	a << 1 << -1 << 0 << (1<<19) << ((1<<31)+8);

	uint64_t ux = (uint64_t)1<<40;
	a << ux;

	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());

	reader r(a);

	int x, y, z, w;
	uint32_t foo;
	uint64_t bar;
	r >> x >> y >> z >> w >> foo >> bar;

	REQUIRE(x == 1);
	REQUIRE(y == -1);
	REQUIRE(z == 0);
	REQUIRE(w == (1<<19));
	REQUIRE(foo == (uint32_t)((1<<31)+8));
	REQUIRE(bar == ux);
}

TEST_CASE("chive/types", "try inserting/removing every possible type") {
	using namespace boost::posix_time;
	using namespace boost::uuids;

	std::string ts("2011-04-12 13:20:10.000");
	ptime t1(time_from_string(ts));

	uuid u1 = nil_uuid();
	uuid u2 = random_generator()();

	const char* ateam = "Ten years ago a crack commando unit was sent to prison by a military court for a crime they didn't commit. These men promptly escaped from a maximum security stockade to the Los Angeles underground.";

	archive a;
	a.append_null();
	a << true << false << 10 << 20
	  << 0xf000 << -904848 << uint64_t(10L << 40)
	  << t1 << u1 << u2 << "why herro thar" << ateam;

	std::vector<int> v;
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	a.append(v);
	a.append((const uint8_t*)"raw data", 8);

	a.done();

	LOG_TRACE("%s\n", a.to_string().c_str());

	archive::iterator i = a.begin();

	REQUIRE(i.type() == Null);
	++i;
	REQUIRE(i.type() == Bool);
	REQUIRE(i.get<bool>() == true);
	++i;
	REQUIRE(i.type() == Bool);
	REQUIRE(i.get<bool>() == false);
	++i;
	REQUIRE(i.type() == SmallInt);
	REQUIRE(i.get<int>() == 10);
	++i;
	REQUIRE(i.type() == Uint8);
	REQUIRE(i.get<int>() == 20);
	++i;
	REQUIRE(i.type() == Uint16);
	REQUIRE(i.get<int>() == 0xf000);
	++i;
	REQUIRE(i.type() == Int);
	REQUIRE(i.get<int>() == -904848);
	++i;
	REQUIRE(i.type() == Uint64);
	REQUIRE(i.get<uint64_t>() == (uint64_t)(10L << 40));
	++i;
	REQUIRE(i.type() == PosixTime);
	REQUIRE(i.get<ptime>() == t1);
	++i;
	REQUIRE(i.type() == NilUUID);
	REQUIRE(i.get<uuid>() == u1);
	++i;
	REQUIRE(i.type() == UUID);
	REQUIRE(i.get<uuid>() == u2);
	++i;
	REQUIRE(i.type() == SmallString);
	REQUIRE(i.get<string>() == "why herro thar");
	++i;
	REQUIRE(i.type() == String);
	REQUIRE(i.get<string>() == ateam);
	++i;

	std::vector<int> v2;
	i.contents(v2);
	REQUIRE(i.type() == List);
	REQUIRE(v == v2);
	++i;

	REQUIRE(i.type() == RawData);
	char datacmp[9];
	memcpy(datacmp, i.body(), 8);
	datacmp[8] = 0;
	REQUIRE(string(datacmp) == "raw data");
	++i;

	REQUIRE(i == a.end());
}

TEST_CASE("chive/biglist", "testing a big archive") {

	archive a;

	srand(149499);
	int num_inserts = 0;
	while (a.size() < 100000) {
		a << rand();
		num_inserts++;
	}

	a.done();

	LOG_TRACE("archive size: %d, num inserts: %d\n", a.size(), num_inserts);

	for (archive::iterator i = a.begin(); i != a.end(); ++i)
		num_inserts--;

	REQUIRE(num_inserts == 0);
}

TEST_CASE("chive/deepnesting", "testing deep nesting") {
	archive a;

	using namespace std;

	typedef vector<string> v0;
	typedef vector<v0> v1;
	typedef vector<v1> v2;
	typedef vector<v2> v3;
	typedef vector<v3> v4;
	typedef vector<v4> v5;
	typedef vector<v5> v6;

	v0 list0;
	v1 list1;
	v2 list2;
	v3 list3;
	v4 list4;
	v5 list5;
	v6 list6;

	list0.push_back("EPIC");
	list0.push_back("LIST");
	list1.push_back(list0);
	list2.push_back(list1);
	list3.push_back(list2);
	list4.push_back(list3);
	list5.push_back(list4);
	list6.push_back(list5);

	a << list0 << list1 << list2 << list3 << list4 << list5 << list6;

	a.done();

	LOG_TRACE("archive size: %d\n", (int)a.size());


	v0 out0;
	v1 out1;
	v2 out2;
	v3 out3;
	v4 out4;
	v5 out5;
	v6 out6;

	archive::iterator i = a.begin();
	(i++).contents(out0);
	(i++).contents(out1);
	(i++).contents(out2);
	(i++).contents(out3);
	(i++).contents(out4);
	(i++).contents(out5);
	(i++).contents(out6);
	REQUIRE(i == a.end());

	string s = a.to_string();
	LOG_TRACE("%s\n", s.c_str());

	REQUIRE(out6.front().front().front().front().front().front().front() == "EPIC");
}
