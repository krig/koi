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

#include "msg.hpp"
#include <boost/function.hpp>
#include <boost/scoped_ptr.hpp>
#include <map>
#include <set>

namespace koi {

	struct nexus_impl;
	struct elector;
	struct runner;
	struct cluster;
	struct masterstate;

	struct nexus : private boost::noncopyable {
		typedef boost::function<void (elector*, msg::request*, msg::response::values&)> elector_rpcfn;
		typedef boost::function<void (runner*, msg::request*, msg::response::values&)> runner_rpcfn;
		typedef std::map<string, elector_rpcfn> elector_rpc_functions;
		typedef std::map<string, runner_rpcfn> runner_rpc_functions;
		typedef std::set<string> stringset;
		typedef boost::scoped_ptr<nexus_impl> pimpl;

		nexus(net::io_service& ioservice, const settings& conf);
		~nexus();

		const settings& cfg() const;
		settings& cfg();
		net::io_service& io() const;
		net::endpoint get_elector() const;
		masterstate get_masterstate() const;
		void set_masterstate(const masterstate& ms);
		bool has_quorum() const;

		void add_link(const net::endpoint& remote);
		void remove_link(const net::endpoint& remote);

		void send(const message& m);
		void send(const message& m, const net::endpoint& to);

		void handle(message& m);

		bool settings_changed(const settings& newcfg);

		bool init();
		void init_links();

		void _route(message& m);

		void up(cluster* c);
		void down(cluster* c);
		void state_change(cluster* c);

		void update();

		bool rpc_handle(msg::request* rq, msg::response::values& data);
		bool rpc_elector(msg::request* rq, msg::response::values& data);
		bool rpc_runner(msg::request* rq, msg::response::values& data);
		bool rpc_recover(msg::request* rq, msg::response::values& data);

		std::pair<bool, net::endpoint> redirect_to(const string& nodename);

		typedef msg::heartbeat::node node;
		typedef std::vector<node> nodelist;
		const nodelist& nodes() const;

		pimpl                 _impl;
		elector_rpc_functions _elector_rpc;
		runner_rpc_functions  _runner_rpc;
		stringset             _redirecting_rpc;
		ptime                 _cluster_t;
	};

}
