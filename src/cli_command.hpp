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

#include <boost/regex.hpp>

struct command;
typedef boost::shared_ptr<command> command_ptr;

struct command_factory {
	virtual ~command_factory() {}
	virtual const char* name() const = 0;
	virtual const char* args() const = 0;
	virtual const char* desc() const = 0;
	virtual bool is(const char* name) const = 0;
	virtual command_ptr create(const vector<string>& args) const = 0;
};

struct command {
	command(const char* cmd, const vector<string>& args) : _cmd(cmd), _args(args) {
	}
	virtual ~command() {}

	virtual net::endpoint prepare(message& m, net::endpoint to) {
		msg::request* rpc = m.set_body<msg::request>();
		rpc->_cmd = _cmd;
		rpc->_args = _args;
		return to;
	}

	virtual command_ptr handle(const message& msg, const net::endpoint& from, int& retval);

	string _cmd;
	vector<string> _args;
};

template <typename Command>
struct command_factory_impl : public command_factory {
	command_factory_impl(const char* n, const char* a, const char* d) : _name(n), _args(a), _desc(d) {}
	virtual ~command_factory_impl() {}
	virtual const char* name() const { return _name.c_str(); }
	virtual const char* args() const { return _args.c_str(); }
	virtual const char* desc() const { return _desc.c_str(); }
	virtual bool is(const char* nm) const { return _name == nm; }
	virtual command_ptr create(const vector<string>& a) const { auto p = boost::shared_ptr<Command>(new Command(this->name(), a)); return p; }

	string _name;
	string _args;
	string _desc;
};

struct command_list {
	typedef boost::shared_ptr<command_factory> factory;

	template <typename Command>
	command_list& add(const char* name, const char* args, const char* desc) {
		command_factory* fac = new command_factory_impl<Command>(name, args, desc);
		factory p(fac);
		_list.push_back(p);
		return *this;
	}

	command_list& add(const char* name, const char* args, const char* desc) {
		return add<command>(name, args, desc);
	}

	command_ptr parse(const string& cmd, const vector<string>& args) {
		for (size_t i = 0; i < size(); ++i) {
			if (_list[i]->is(cmd.c_str())) {
				return _list[i]->create(args);
			}
		}
		LOG_ERROR("Command not found: %s", cmd.c_str());
		return command_ptr();
	}

	size_t size() const { return _list.size(); }

	factory& operator[](int idx) { return _list[idx]; }
	const factory& operator[](int idx) const { return _list[idx]; }

	vector<factory> _list;
};

struct tree_command : public command {
	tree_command(const char* cmd, const vector<string>& args) : command(cmd, args) {
	}

	struct service {
		string _name;
		string _state;

		string str() const {
			if (_state.size())
				return _name + " : " + _state;
			service_info inf = parse_service_info(_name);
			string ret = inf._name + " : " + string(service_state_string(inf._state));
			if (inf._event.size() && inf._event != "none")
				ret += " (executing: " + inf._event + ")";
			if (inf._failed)
				ret += " [FAILED]";
			return ret;
		}
	};

	struct node {
		string _name;
		string   _uuid;
		string _addr;
		string _state;
		vector<service> _children;
	};

	void parse_status(vector<node>& nodes, const msg::response* rs) {
		regex rx_name("^[0-9]+-name$");
		regex rx_uuid("^[0-9]+-uuid$");
		regex rx_addr("^[0-9]+-addr$");
		regex rx_state("^[0-9]+-state$");
		regex rx_services("^[0-9]+-services$");
		msg::response::values::const_iterator i = rs->_response.begin();
		msg::response::values::const_iterator e = rs->_response.end();
		for (; i != e; ++i) {
			if (regex_match(i->first, rx_name)) {
				intmax_t n = strtol(i->first.c_str(), 0, 10);
				if ((int)nodes.size() <= n)
					nodes.resize(n+1);
				nodes[n]._name = get<string>(i->second);
			}
			else if (regex_match(i->first, rx_uuid)) {
				intmax_t n = strtol(i->first.c_str(), 0, 10);
				if ((int)nodes.size() <= n)
					nodes.resize(n+1);
				nodes[n]._uuid = lexical_cast<string>(get<uuid>(i->second));
			}
			else if (regex_match(i->first, rx_addr)) {
				intmax_t n = strtol(i->first.c_str(), 0, 10);
				if ((int)nodes.size() <= n)
					nodes.resize(n+1);
				nodes[n]._addr = get<string>(i->second);
			}
			else if (regex_match(i->first, rx_state)) {
				intmax_t n = strtol(i->first.c_str(), 0, 10);
				if ((int)nodes.size() <= n)
					nodes.resize(n+1);
				nodes[n]._state = state_to_string((koi::State)get<int>(i->second));
			}
			else if (regex_match(i->first, rx_services)) {
				vector<string> svcs = get<vector<string>>(i->second);
				for (size_t s = 0; s < svcs.size(); ++s) {
					intmax_t n = strtol(i->first.c_str(), 0, 10);
					if ((int)nodes.size() <= n)
						nodes.resize(n+1);
					service ss;
					ss._name = svcs[s];
					ss._state = "";
					nodes[n]._children.push_back(ss);
				}
			}
		}
	}

	void draw_tree(const vector<node>& nodes) {
		for (size_t i = 0; i < nodes.size()-1; ++i) {
			cout << "|-- "
			     << nodes[i]._name << " [" << nodes[i]._uuid << "]"
			     << " : "
			     << nodes[i]._state
			     << endl;
			if (nodes[i]._children.size()) {
				for (size_t j = 0; j < nodes[i]._children.size()-1; ++j) {
					cout << "|   |-- "
					     << nodes[i]._children[j].str()
					     << endl;
				}
				size_t j = nodes[i]._children.size()-1;
				cout << "|   `-- "
				     << nodes[i]._children[j].str()
				     << endl;
			}
		}
		if (!nodes.empty()) {
			size_t i = nodes.size()-1;
			cout << "`-- "
			     << nodes[i]._name << " [" << nodes[i]._uuid << "]"
			     << " : "
			     << nodes[i]._state << endl;
			if (nodes[i]._children.size()) {
				for (size_t j = 0; j < nodes[i]._children.size()-1; ++j) {
					cout << "    |-- "
					     << nodes[i]._children[j].str()
					     << endl;
				}
				size_t j = nodes[i]._children.size()-1;
				cout << "    `-- "
				     << nodes[i]._children[j].str()
				     << endl;
			}
		}
	}

	string clr(int c, const string& txt) {
		string ret("\e[");
		ret += lexical_cast<string>(c);
		ret += "m";
		ret += txt;
		ret += "\e[0m";
		return ret;
	}

	void draw_tree_color(const vector<node>& nodes) {
		using namespace koi::logging;

		for (size_t i = 0; i < nodes.size()-1; ++i) {
			cout << "|-- "
			     << clr(GREEN, nodes[i]._name) << " [" << nodes[i]._uuid << "]"
			     << " : "
			     << clr(YELLOW, nodes[i]._state)
			     << endl;
			if (nodes[i]._children.size()) {
				for (size_t j = 0; j < nodes[i]._children.size()-1; ++j) {
					cout << "|   |-- "
					     << clr(BLUE, nodes[i]._children[j].str())
					     << endl;
				}
				size_t j = nodes[i]._children.size()-1;
				cout << "|   `-- "
				     << clr(BLUE, nodes[i]._children[j].str())
				     << endl;
			}
		}
		if (!nodes.empty()) {
			size_t i = nodes.size()-1;
			cout << "`-- "
			     << clr(GREEN, nodes[i]._name) << " [" << nodes[i]._uuid << "]"
			     << " : "
			     << clr(YELLOW, nodes[i]._state) << endl;
			if (nodes[i]._children.size()) {
				for (size_t j = 0; j < nodes[i]._children.size()-1; ++j) {
					cout << "    |-- "
					     << clr(BLUE, nodes[i]._children[j].str())
					     << endl;
				}
				size_t j = nodes[i]._children.size()-1;
				cout << "    `-- "
				     << clr(BLUE, nodes[i]._children[j].str())
				     << endl;
			}
		}
	}

	virtual command_ptr handle(const message& m, const net::endpoint& from, int& retval) {
		if (m._op == msg::base::Response) {
			const msg::response* rs = m.body<msg::response>();
			if (rs->_response.find("redirect") != rs->_response.end()) {
				return command::handle(m, from, retval);
			}

			if (rs->_response.find("cmd") == rs->_response.end())
				return command::handle(m, from, retval);
			const string cmd = get<string>(rs->_response.find("cmd")->second);
			if (cmd != "tree")
				return command::handle(m, from, retval);

			// draw tree..
			bool mt = false;
			if (rs->_response.find("maintenance") != rs->_response.end())
				mt = get<bool>(rs->_response.find("maintenance")->second);
			cout << "/koi " << (mt ? "(maintenance)" : "") << endl;

			vector<node> nodes;
			parse_status(nodes, rs);

			if (!nodes.empty()) {
				if (koi::logging::colors)
					draw_tree_color(nodes);
				else
					draw_tree(nodes);
			}
			retval = 0;
			return command_ptr();
		}
		return command::handle(m, from, retval);
	}
};

struct redirect_command : public tree_command {
	redirect_command(const char* cmd, const vector<string>& args, const net::endpoint& to) : tree_command(cmd, args), _redirect(to) {
	}

	virtual net::endpoint prepare(message& m, net::endpoint) {
		return tree_command::prepare(m, _redirect);
	}

	virtual command_ptr handle(const message& msg, const net::endpoint& from, int& retval) {
		return tree_command::handle(msg, from, retval);
	}

	net::endpoint _redirect;
};


namespace {
	inline string value_for_entry(const string& key, const msg::rpc_variant& val) {
		return apply_visitor(variant_printer(key), val);
	}
}

command_ptr command::handle(const message& m, const net::endpoint&, int& retval) {
	if (koi::debug_mode) {
		cout << "From: " << m._sender_uuid << endl;
	}

	if (m._op == msg::base::StateUpdate) {
		const msg::stateupdate* su = m.body<msg::stateupdate>();
		cout << "Master node: " << su->_master_uuid << endl;
		cout << "Uptime: " << su->_uptime << " ms" << endl;
	}
	else if (m._op == msg::base::HealthReport) {
		const msg::healthreport* hr = m.body<msg::healthreport>();
		cout << "Name: " << hr->_name << endl
		     << "Uptime: " << hr->_uptime << " ms" << endl
		     << "State: " << hr->_state << endl
		     << "Maintenance: " << hr->_maintenance << endl
		     << "Target action: " << hr->_service_action << endl;
		for (size_t i = 0; i < hr->_services.size(); ++i)
			cout << "Service: " << hr->_services[i].to_string() << endl;
	}
	else if (m._op == msg::base::Response) {
		const msg::response* rs = m.body<msg::response>();

		if (rs->_response.find("redirect") != rs->_response.end()) {
			retval = -1;
			const string redirect = boost::get<string>(rs->_response.find("redirect")->second);
			const string cmd = get<string>(rs->_response.find("cmd")->second);
			const std::vector<string> args = get<std::vector<string>>(rs->_response.find("args")->second);
			if (koi::debug_mode) {
				cout << "Redirect: " << redirect << " " << cmd << endl;
			}
			boost::shared_ptr<redirect_command> p(new redirect_command(cmd.c_str(), args, parse_endpoint(redirect.c_str(), KOI_DEFAULT_CLUSTER_PORT)));
			return p;
		}
		else {
			typedef map<string, string> values;
			values generic;
			typedef map<int, values> subnodes_map;
			subnodes_map subnodes;

			// gief auto!
			regex rx_subnode("^([0-9]+)-(.*)$");
			FOREACH(const auto& entry, rs->_response) {
				match_results<string::const_iterator> what;
				if (regex_match(entry.first, what, rx_subnode)) {
					int snode = lexical_cast<int>(what[1]);
					values& vals = subnodes[snode];
					string name(what[2].first, what[2].second);
					vals[name] = value_for_entry(name, entry.second);
				}
				else {
					generic[entry.first] = value_for_entry(entry.first, entry.second);
				}
			}


			if (generic.find("cmd") != generic.end()) {
				//cout << generic["cmd"] << endl;
				generic.erase("cmd");
			}
			if (generic.find("msg") != generic.end()) {
				cout << generic["msg"] << endl;
				generic.erase("msg");
			}
			FOREACH(const auto& v, generic)
				cout << v.first << ": " << v.second << endl;

			cout << endl;

			FOREACH(const auto& sn, subnodes) {
				cout << "[node]" << endl;
				FOREACH(const auto& v, sn.second)
					cout << v.first << ": " << v.second << endl;
				cout << "[end]" << endl;
			}
		}
	}
	else {
		cout << "Client got unknown message " << m._op << endl;
		retval = 1;
		return command_ptr();
	}
	retval = 0;
	return command_ptr();
}
