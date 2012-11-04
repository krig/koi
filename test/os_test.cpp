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
#include "os.hpp"
#include "file.hpp"
#include "strfmt.hpp"

using namespace std;
using namespace koi;

TEST_CASE("os/random", "these are a bit system dependant..") {
	using namespace os;
	using namespace os::path;

	REQUIRE(isdir("/tmp"));
	REQUIRE(!isdir("/thisishopefullynotarealdirectory"));
	REQUIRE(isexec("/usr/bin/env"));
	REQUIRE(!isexec("/etc/hosts"));
	REQUIRE(exists("/etc/hosts"));
	REQUIRE(!exists("/thisishopefulynotanexistingfile"));

	REQUIRE(string(path::basename("/path/fil.ext")) == "fil.ext");
	REQUIRE(string(path::extension("fil.ext")) == "ext");
	REQUIRE(os::path::path("/path/fil.ext") == "/path/");
	REQUIRE(read_file("/etc/hosts").length() > 0);

	REQUIRE(string(os::environ("PATH")).length() > 0);

	int i = 13;
	while (exists(strfmt<64>("/tmp/koi-os-%d", i)))
		++i;

	strfmt<128> dnam("/tmp/koi-os-%d/s1/s2", i);
	REQUIRE(makepath(dnam));
	REQUIRE(isdir(dnam));
}
