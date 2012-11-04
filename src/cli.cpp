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
#include "koi.hpp"
#include "msg.hpp"
#include <unistd.h>

#include <boost/program_options.hpp>
#include <iostream>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <sstream>
#include <iomanip>
#include <signal.h>

#include "hex.hpp"

using namespace std;
using namespace boost;
using namespace boost::program_options;
using boost::asio::deadline_timer;
using boost::asio::ip::udp;
using namespace boost::posix_time;
using namespace koi;

// send messages/commands to cluster nodes

static const int KOI_CLIENT_PORT = 0;

template<typename T>
ostream & operator<<(ostream & o, const vector<T>& v) {
	return o << "[" << join(v.begin(), v.end(), ", ") << "]";
}

#include "variant_printer.hpp"
#include "cli_command.hpp"

namespace koi {
	void force_reload_config() {} // stub implementation
}

namespace {
	bool interrupted = false;
	int command_timeout = 6;

	void sigint_handler(int) {
		LOG_WARN("Interrupting...");
		interrupted = true;
	}

	struct client {
		client(asio::io_service& io_service, net::endpoint server, const uuid& id, const char* pass)
			: _complete(-1),
			  _io_service(io_service),
			  _socket(io_service),
			  _server(server),
			  _starttime(microsec_clock::local_time()),
			  _id(id),
			  _pass(pass) {
			_init_socket(net::endpoint(net::ipaddr::from_string("0.0.0.0"), KOI_CLIENT_PORT));
		}
		~client() {
		}

		void _init_socket(const net::endpoint& listen) {
			const uint16_t max_inc = 1000;
			uint16_t inc = 0;
			net::endpoint actual(listen);
			for (;;) {
				boost::system::error_code ec;
				_socket.open(actual.protocol(), ec);
				if (!ec) {
					_socket.bind(actual, ec);
				}
				if (ec) {
					if (inc < max_inc) {
						actual.port(listen.port() + (++inc));
						if (_socket.is_open())
							_socket.close(ec);
						continue;
					}
					else {
						LOG_INFO("Address %s already in use.", to_string(actual).c_str());
						throw std::runtime_error("Address already in use.");
					}
				}
				break;
			}
		}

		void send_message(message& m, net::endpoint& to) {
			vector<uint8_t> data;
			msg::encode(data, &m, _pass.c_str());
			_socket.async_send_to(asio::buffer(data, data.size()), to,
			                      bind(&client::handle_send_to, this,
			                           asio::placeholders::error,
			                           asio::placeholders::bytes_transferred));

			_response_buffer.resize(msg::MAX_MSG_LEN);
			_socket.async_receive_from(
			                           asio::buffer(_response_buffer), _response_endpoint,
			                           bind(&client::handle_receive_from, this,
			                                asio::placeholders::error,
			                                asio::placeholders::bytes_transferred));
		}

		bool sending() const {
			return _complete < 0 && !interrupted;
		}

		int execute_command(const command_ptr& cmd) {
			signal(SIGINT, sigint_handler);

			_command = cmd;

			message m(_id);

			ptime resend_time;
			ptime now;
			while (sending()) {
				now = microsec_clock::local_time();
				_server = _command->prepare(m, _server);
				resend_time = now + seconds(2);
				send_message(m, _server);

				while (sending() && (now < resend_time)) {
					if (command_timeout > 0 && (now - _starttime > seconds(command_timeout))) {
						cerr << "Request timed out.\n";
						interrupted = true;
						break;
					}

					_io_service.poll();

					usleep(1000);

					now = microsec_clock::local_time();
				}
			}
			return interrupted ?  1 : _complete;
		}

		void handle_send_to(const boost::system::error_code& err, size_t nbytes) {
			if (err) {
				cerr << "Sending request failed (" << nbytes << " bytes).\n";
				_complete = 1;
			}
		}

		void handle_receive_from(const boost::system::error_code& err,
		                         size_t bytes_recvd) {
			_response_buffer.resize(bytes_recvd);
			if (err) {
				cerr << "Error receiving response.\n";
				_complete = 1;
				return;
			}
			message m;
			if (!msg::decode(&m, _response_buffer, _pass.c_str())) {
				cerr << "Failed to decode response of size " << bytes_recvd << " from buffer of size " << _response_buffer.size() << ".\n";
				_complete = 1;
				return;
			}

			command_ptr next = _command->handle(m, _response_endpoint, _complete);
			if (next) {
				_command = next;
				message mg(_id);
				_server = _command->prepare(mg, _server);
				send_message(mg, _server);
				_complete = -1;
			}
		}

		int               _complete;
		asio::io_service& _io_service;
		udp::socket       _socket;
		net::endpoint     _server;
		vector<uint8_t>   _response_buffer;
		net::endpoint     _response_endpoint;
		command_ptr       _command;
		ptime             _starttime;
		uuid              _id;
		string            _pass;
	};

	int execute(const char* host, int port, command_ptr cmd, settings& cfg) {
		if (!cmd) {
			cerr << "Unknown command." << endl;
			return 1;
		}
		asio::io_service io_service;
		client c(io_service, net::endpoint(net::ipaddr::from_string(host), port), cfg._uuid, cfg._pass.c_str());
		return c.execute_command(cmd);
	}
}

extern const char* g_program_build;

int main(int argc, char* argv[]) {
	settings cfg;
	try {
		options_description generic("Generic options");
		generic.add_options()
			("version,v", "print version string")
			("help,h", "produce help message")
			;

		vector<string> configs = split(KOI_CONFIG_FILES, ";");

		options_description desc("Options");
		desc.add_options()
			("host,H", value<string>(), "host to connect to (default local)")
			("port,p", value<int>(), "port to connect to (default local)")
			("secret", value<string>(), "cluster shared secret")
			("id", value<int>(), "cluster id")
			("color,c", "Enable color output")
			("debug,d", "debug mode")
			("timeout,t", value<int>()->default_value(command_timeout), "Call timeout in seconds")
			("file,f", value<vector<string>>()->default_value(configs, string(KOI_CONFIG_FILES)),
			 "configuration file");


		options_description hidden("Hidden options");
		hidden.add_options()
			("exec,e", value<vector<string>>(), "command to execute")
			;

		options_description cmdline_options("Allowed options");
		cmdline_options.add(generic).add(desc).add(hidden);

		options_description visible("");
		visible.add(generic).add(desc);

		positional_options_description p;
		p.add("exec", -1);

		variables_map vm;
		command_line_parser clp(argc, argv);
		clp.options(cmdline_options);
		clp.positional(p);
		store(clp.run(), vm);
		notify(vm);

		if (vm.count("version")) {
			cout << "koi cli " << koi::version << " (" << g_program_build << ")\n";
			return 0;
		}

		command_timeout = vm["timeout"].as<int>();

		logging::colors = vm.count("color");

		command_list commands;
		commands
			.add("local", "", "Local node status information.")
			.add("status", "[node]", "Node/cluster status information.")
			.add<tree_command>("tree", "", "Cluster status formatted as a tree.")
			.add("reconfigure", "[node]", "Reload the configuration file.")
			.add("maintenance", "on|off", "Set the cluster in maintenance mode.")
			.add("promote", "<name>|<uuid>", "Promote given node to master.")
			.add("demote", "", "Demote any current master (no new master is elected).")
			.add("elect", "", "Elect a new master after manual demotion.")
			.add("start", "[node]", "Start services on a stopped node.")
			.add("stop", "[node]", "Demote and stop services on a node.")
			.add("recover", "[node]", "Recover any failed nodes.")
			.add("failures", "", "List recent failures.");

		if (vm.count("help")) {
			cout.setf(ios::left, ios::adjustfield);
			cout << "Usage: " << argv[0] << " [options] <command> [args..]\n"
			     << visible << endl
			     << "Available commands:" << endl;
			for (size_t i = 0; i < commands.size(); ++i) {
				string np = commands[i]->name();
				if (strlen(commands[i]->args()) > 0) {
					np += " ";
					np += commands[i]->args();
				}
				cout << setw(22) << np << setw(60) << commands[i]->desc() << endl;
			}
			cout.unsetf(ios::left);
			return 1;
		}

		if (vm.count("debug"))
			koi::debug_mode = true;

		if (vm.count("exec")) {
			if (!cfg.boot(vm["file"].as<vector<string>>(), false))
				return 1;

			if (vm.count("secret"))
				cfg._pass = vm["secret"].as<string>();

			if (vm.count("id"))
				cfg._cluster_id = vm["id"].as<int>();

			vector<string> cmd = vm["exec"].as<vector<string>>();
			if (!cmd.empty()) {
				string a0 = cmd.front();
				cmd.erase(cmd.begin());

				string host = net::ipaddr().to_string();
				int port = cfg._port;

				if (vm.count("host"))
					host = vm["host"].as<string>();
				if (vm.count("port"))
					port = vm["port"].as<int>();

				return execute(host.c_str(), port, commands.parse(a0, cmd), cfg);
			}
		}
		cout << "No command specified.\n";
	}
	catch (unknown_option& e) {
		cerr << e.what() << endl;
		return 1;
	}
	catch (boost::system::system_error& e) {
		cerr << "Terminating due to error: "
		     << e.code() << " "
		     << e.what() << endl;
		return 1;
	}
	catch (std::exception& e) {
		cerr << "Terminating due to error: "
		     << e.what() << endl;
		return 1;
	}
	return 0;
}
