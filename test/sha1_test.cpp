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
#include "sha1.hpp"

namespace {
	std::string sha1(const char* str) {
		return SHA1().update((const uint8_t*)str, strlen(str)).end().hex();
	}
}

namespace Catch {
	template<>
	std::string toString<SHA1::digest >( const SHA1::digest& value )
	{
		std::ostringstream oss;
		oss << value.hex();
		return oss.str();
	}
}

TEST_CASE("sha1/verify_hashes", "compare against some known hashes") {
	SHA1::digest ninjas = SHA1().update((const uint8_t*)"ninjas", 6).end();
	SHA1::digest nonjas = SHA1().update((const uint8_t*)"nonjas", 6).end();

	REQUIRE(ninjas != nonjas);
	REQUIRE(ninjas.at32(0) != nonjas.at32(0));
	REQUIRE(ninjas.hex() == "e48b5405291223b2ff1d676a273d0ed8abc1adf7");
	REQUIRE(nonjas.hex() == "00c20d940e68767caaa0ac535652eafe6d6bf7b0");

	REQUIRE(sha1("The quick brown fox jumps over the lazy dog") == "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
	REQUIRE(sha1("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}
