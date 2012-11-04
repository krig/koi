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
#include "crypt.hpp"
#include "sha1.hpp"

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>

using namespace std;
using namespace koi;

namespace {
	SHA1::digest hashpass(const char* pass) {
		SHA1 sha1;
		sha1.update((const uint8_t*)pass, strlen(pass));
		return sha1.end();
	}

	std::string g_chars(
	                    "abcdefghijklmnopqrstuvwxyz"
	                    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
	                    "1234567890"
	                    "!@#$%^&*()"
	                    "`~-_=+[{]{\\|;:'\",<.>/? ");
	boost::uniform_int<> g_index_dist(0, g_chars.size() - 1);
	boost::mt19937 g_gen;

	boost::variate_generator<boost::mt19937&, boost::uniform_int<>> g_chargen(g_gen, g_index_dist);

	string generate_random_string(int lo, int hi) {
		boost::uniform_int<> dist(lo, hi);
		boost::variate_generator<boost::mt19937&, boost::uniform_int<>> die(g_gen, dist);
		int len = die();
		string out;
		out.reserve(len);
		for(int i = 0; i < len; ++i) {
			out += g_chars[g_chargen()];
		}

		return out;
	}
}

TEST_CASE("crypto/basic", "some very simple tests of the encrypt/decrypt functions") {
	char buf[36] = "One clever fox is not a happy dog";

	SHA1::digest d = hashpass("Secret password");

	crypto::encrypt((uint8_t*)buf, 36, d._data32, 5);
	REQUIRE(string(buf) != "One clever fox is not a happy dog");

	crypto::decrypt((uint8_t*)buf, 36, d._data32, 5);
	REQUIRE(string(buf) == "One clever fox is not a happy dog");

	char buf2[36] = "One clever fox is not a happy dog";

	SHA1::digest d2 = hashpass("Secret password2");

	crypto::encrypt((uint8_t*)buf, 36, d._data32, 5);
	crypto::encrypt((uint8_t*)buf2, 36, d2._data32, 5);
	REQUIRE(string(buf) != string(buf2));

	crypto::decrypt((uint8_t*)buf, 36, d2._data32, 5);
	REQUIRE(string(buf) != "One clever fox is not a happy dog");
}

TEST_CASE("crypto/looped", "try encrypting/decrypting random data") {
	for (int i = 0; i < 6000; ++i) {
		char cmp[256];
		char cmp2[256];
		memset(cmp, 0, 256);
		memset(cmp2, 0, 256);
		strcpy(cmp, generate_random_string(30, 150).c_str());
		strcpy(cmp2, cmp);
		string hpass = generate_random_string(10, 30);
		SHA1::digest dig = hashpass(hpass.c_str());
		crypto::encrypt((uint8_t*)cmp, 256, dig._data32, 5);
		REQUIRE(string(cmp) != string(cmp2));
		crypto::decrypt((uint8_t*)cmp, 256, dig._data32, 5);
		REQUIRE(string(cmp) == string(cmp2));
	}
}
