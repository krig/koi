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
#include <boost/shared_ptr.hpp>
#include <boost/uuid/nil_generator.hpp>
#include <map>
#include <boost/variant.hpp>

#include "service_info.hpp"
#include "archive.hpp"
#include "mru.hpp"

namespace koi {
	archive& operator<<(archive& a, const net::endpoint& ep);
	reader&  operator>>(reader&  r, net::endpoint& ep);
	archive& operator<<(archive& a, const mru<net::endpoint>& m);
	reader&  operator>>(reader&  r, mru<net::endpoint>& m);

	namespace msg {
		struct base {
			virtual ~base() {}

			enum Type {
				HealthReport, // sent from runner -> elector
				StateUpdate, // sent from elector -> runner
				Request, // sent from client to cluster|elector|runner
				Response, // sent from cluster|elector|runner to client
				HeartBeat, // sent from cluster to cluster
				NumOps
			};
		};

		inline const char* type_to_string(base::Type t) {
			static const char* ts[] = { "HealthReport", "StateUpdate", "Request", "Response", "HeartBeat" };
			if (t >= 0 && t < (int)ASIZE(ts))
				return ts[t];
			return "Unknown";
		}

		struct message {
			int _version; // version matching
			uint32_t _seqnr; // sequence number (to counteract reordering)
			base::Type _op; // what kind of operation is it
			uint8_t _cluster_id;
			uuid _sender_uuid;
			net::endpoint _from;
			boost::shared_ptr<msg::base> _body;

			template <typename Body>
			Body* set_body();

			template <typename Body>
			const Body* body() const;

			template <typename Body>
			Body* body();

			message() : _version(koi::version),
			            _seqnr(0),
			            _op(base::NumOps),
			            _cluster_id(0),
			            _sender_uuid(boost::uuids::nil_uuid()),
			            _from() {}
			message(const uuid& uuid, uint8_t cluster_id)
				: _version(koi::version),
				  _seqnr(0),
				  _op(base::NumOps),
				  _cluster_id(cluster_id),
				  _sender_uuid(uuid),
				  _from() {
			}
			message(const uuid& uuid, uint8_t cluster_id, base::Type op)
				: _version(koi::version),
				  _seqnr(0),
				  _op(op),
				  _cluster_id(cluster_id),
				  _sender_uuid(uuid),
				  _from() {
			}
		};

		struct healthreport : public base {
			// sent from runner to elector
			virtual ~healthreport() {}

			string _name;
			uint64_t _uptime; // uptime in milliseconds
			State _state;
			RunnerMode _mode;
			bool _maintenance;
			ServiceAction _service_action;

			typedef std::vector<service_info> services;
			services _services;
		};

		struct stateupdate : public base {
			// sent from elector to runner
			virtual ~stateupdate() {}

			uint64_t _uptime;
			bool _maintenance;

			// master node information
			uuid _master_uuid;
			ptime _master_last_seen;
			string _master_name;
			net::endpoint _master_addr;
		};

		struct request : public base {
			virtual ~request() {}

			string _cmd;
			std::vector<string> _args;
		};

		typedef boost::variant<
			bool,
			int,
			string,
			uuid,
			std::vector<string>,
			std::vector<uint8_t>,
			ptime
			> rpc_basevariant;

		struct rpc_variant : public rpc_basevariant {
			rpc_variant() : rpc_basevariant() {}
			template<typename T>
			rpc_variant(const T& t) : rpc_basevariant(t) {}

			rpc_variant(const char* str) : rpc_basevariant(string(str)){}
		};

		struct response : public base {
			virtual ~response() {}

			typedef rpc_variant value;
			typedef std::map<string, value> values;

			values _response;
		};

		struct heartbeat : public base {
			virtual ~heartbeat() {}

			struct node {
				node() :
					_id(boost::uuids::nil_uuid()),
					_name(),
					_last_seen(boost::posix_time::min_date_time),
					_flags(0) {}
				node(const uuid& id,
				     const string& name,
				     int flags,
				     const net::endpoint& addr) :
					_id(id),
					_name(name),
					_last_seen(boost::posix_time::min_date_time),
					_flags(flags),
					_addrs() {
					_addrs.insert(addr);
				}
				node(const node& n) : _id(n._id), _name(n._name), _last_seen(n._last_seen),
				                      _flags(n._flags), _addrs(n._addrs) {
				}

				node& operator=(const node& n) {
					if (n != *this) {
						_id = n._id;
						_name = n._name;
						_last_seen = n._last_seen;
						_flags = n._flags;
						_addrs = n._addrs;
					}
					return *this;
				}

				bool operator!=(const node& n) const {
					return (_id != n._id) ||
						(_name != n._name) ||
						(_addrs != n._addrs) ||
						(_flags != n._flags);
				}

				uuid _id;
				string _name;
				ptime _last_seen;
				int _flags;
				mru<net::endpoint> _addrs;
			};

			typedef std::vector<node> nodes;

			string _name;
			int _flags;
			nodes _nodes;
			uuid _elector;
			uuid _master;
		};

#define MessageBodyMethods(id, cls)	  \
		template <> inline \
		cls* message::set_body() { \
			_op = base:: id; \
			cls* b = new cls; \
			_body.reset(b); \
			return b; \
		} \
		template <> inline \
		const cls* message::body() const { \
			if (_op == base:: id) \
				return dynamic_cast<const cls*>(_body.get()); \
			return 0; \
		} \
		template <> inline \
		cls* message::body() { \
			if (_op == base:: id) \
				return dynamic_cast<cls*>(_body.get()); \
			return 0; \
		}

		MessageBodyMethods(HealthReport, healthreport)
		MessageBodyMethods(StateUpdate, stateupdate)
		MessageBodyMethods(Request, request)
		MessageBodyMethods(Response, response)
		MessageBodyMethods(HeartBeat, heartbeat)

#undef MessageBodyMethods

		bool encode(std::vector<uint8_t>& to, const message* msg, const string& pass);
		bool decode(message* msg, std::vector<uint8_t>& from, const string& pass);

		enum { MAX_MSG_LEN = 8000 };
	}

	using msg::message;
}

