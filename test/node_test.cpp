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
#include "sequence.hpp"
#include "network.hpp"
#include "masterstate.hpp"
#include "clusterstate.hpp"
#include "cluster.hpp"
#include "settings.hpp"

#include <boost/uuid/name_generator.hpp>

using namespace koi;
using namespace boost;
using namespace boost::uuids;

TEST_CASE("node/clusterstate", "updating cluster state") {
	uuid dns_namespace_uuid;
	name_generator gen(dns_namespace_uuid);
	clusterstate cs;

	uuid a = gen("a");
	uuid b = gen("b");

	bool changed;

	changed = cs.update(a, "a", NodeFlag_Elector, parse_endpoint("192.168.1.1", 1));
	REQUIRE(changed == true);

	changed = cs.update(b, "b", NodeFlag_Runner, parse_endpoint("192.168.1.1", 2));
	REQUIRE(changed == true);

	changed = cs.update(a, "a", NodeFlag_Elector, parse_endpoint("192.168.1.1", 1));
	REQUIRE(changed == false);

}
