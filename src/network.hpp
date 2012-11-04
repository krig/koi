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
#pragma once

namespace koi {
	struct nexus;
	struct clusterstate;

	struct network {
		typedef std::map<uuid, net::endpoint> endpointmap;
		typedef std::list<message> messagequeue;

		network(nexus& route, int netid);

		void handle(message& m);
		void send(const uuid& ID, const string& name, int flags);
		void send(const uuid& ID, const string& name, const clusterstate& s, int flags);
		void sendto(const uuid& to, const uuid& ID, const string& name, int flags);
		message recv(const uuid& ID);
		net::endpoint whois(const uuid& id) const;

		endpointmap  _endpoints;
		messagequeue _in_queue;
		sequence     _sequence;
		nexus& _nexus;
		int _netid;
	};
}
