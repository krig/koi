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
#include "archive.hpp"
#include "crypt.hpp"
#include "sha1.hpp"

#include "hex.hpp"
#include "strfmt.hpp"
#include "static_vector.hpp"

#include <boost/random/mersenne_twister.hpp>
#include <boost/random/uniform_int.hpp>
#include <boost/random/variate_generator.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/variant.hpp>

#define MINIZ_HEADER_FILE_ONLY
#include "miniz.c"


using namespace std;
using namespace boost;
using namespace koi;
using namespace boost::posix_time;
using namespace boost::uuids;

namespace {
	static const int COMPRESSION_THRESHOLD = 500; // messages larger than this are compressed

	struct msg_error : public std::exception {
		static const int SIZE = 1024;
		char _msg[SIZE];
		explicit msg_error(const char* fmt, ...) {
			va_list va_args;
			va_start(va_args, fmt);
			vsnprintf(_msg, SIZE, fmt, va_args);
			va_end(va_args);
		}
		char const* what() const throw() {
			return _msg;
		}
	};

	SHA1::digest hashpass(const char* pass) {
		SHA1 sha1((const uint8_t*)pass, strlen(pass));
		return sha1.end();
	}

	void append_nonce(vector<uint8_t>& to, int nonce) {
		const size_t tosize = to.size();
		to.resize(tosize + 4);
		to[tosize] = nonce&0xff;
		to[tosize+1] = (nonce>>8)&0xff;
		to[tosize+2] = (nonce>>16)&0xff;
		to[tosize+3] = (nonce>>24)&0xff;
	}

	int extract_nonce(vector<uint8_t>& from) {
		if (from.size() < 4)
			return 0;
		const size_t fromsize = from.size() - 4;
		const int nonce = (from[fromsize]&0xff) | ((from[fromsize+1]&0xff)<<8) | ((from[fromsize+2]&0xff)<<16) | ((from[fromsize+3]&0xff)<<24);
		from.resize(fromsize);
		return nonce;
	}

	struct response_appender : public static_visitor<> {
		response_appender() : static_visitor<>(), _builder(0) {}
		response_appender(const response_appender& a) : static_visitor<>(), _builder(a._builder) {}
		response_appender(archive& a) : _builder(&a) {}
		void operator()(bool value) const { _builder->append(value); }
		void operator()(int value) const { _builder->append(value); }
		void operator()(const string& value) const { _builder->append(value); }
		void operator()(const uuids::uuid& value) const { _builder->append(value); }
		void operator()(const vector<string>& value) const { _builder->append(value); }
		void operator()(const ptime& value) const { _builder->append(value); }
		void operator()(const vector<uint8_t>& value) const { _builder->append(value.data(), value.size()); }

		archive* _builder;
	};

	using namespace koi;
	using namespace koi::msg;

	void write_archive(archive& a, const healthreport* hr) {
		a << hr->_name
		  << hr->_uptime
		  << (int)hr->_state
		  << (int)hr->_mode
		  << hr->_maintenance
		  << (int)hr->_service_action
		  << (int)hr->_services.size();
		FOREACH(const service_info& si, hr->_services)
			a << si._name
			  << si._event
			  << (int)si._state
			  << si._failed;
	}

	void read_archive(reader& r, healthreport* hr) {
		int state, mode;
		r >> hr->_name
		  >> hr->_uptime
		  >> state
		  >> mode;
		if (state < 0 || state > S_NumStates)
			throw msg_error("Invalid state: %d", state);

		if (mode < 0 || mode > R_NumModes)
			throw msg_error("Invalid mode: %d", mode);

		hr->_state = (koi::State)state;
		hr->_mode = (koi::RunnerMode)mode;

		r >> hr->_maintenance;

		int saction;
		r >> saction;
		if (validate_service_action(saction) < 0)
			throw msg_error("Invalid action: %d", saction);

		hr->_service_action = (koi::ServiceAction)saction;
		hr->_services.clear();
		uint32_t nservices;
		r >> nservices;
		for (size_t i = 0; i < (size_t)nservices; ++i) {
			int sstate;
			service_info inf;
			r >> inf._name
			  >> inf._event
			  >> sstate
			  >> inf._failed;
			if (validate_service_state(sstate) < 0)
				throw msg_error("Invalid service state: %d", sstate);

			inf._state = (koi::ServiceState)sstate;
			hr->_services.push_back(inf);
		}
	}

	void write_archive(archive& a, const stateupdate* su) {
		a << su->_uptime
		  << su->_master_uuid;
		if (!su->_master_uuid.is_nil()) {
			a << su->_master_last_seen
			  << su->_master_name
			  << su->_master_addr;
		}
	}

	void read_archive(reader& r, stateupdate* su) {
		r >> su->_uptime
		  >> su->_master_uuid;
		if (!su->_master_uuid.is_nil()) {
			r >> su->_master_last_seen
			  >> su->_master_name
			  >> su->_master_addr;
		}
		else {
			su->_master_last_seen = ptime(min_date_time);
			su->_master_name = "";
			su->_master_addr = net::endpoint();
		}
	}

	void write_archive(archive& a, const heartbeat* hb) {
		a << hb->_name
		  << hb->_flags;
		if (hb->_elector.is_nil()) {
			a << false;
		}
		else {
			a << true
			  << hb->_elector
			  << hb->_master
			  << hb->_cluster_maintenance
			  << (int)hb->_nodes.size();
			FOREACH(heartbeat::node const& n, hb->_nodes) {
				a << n._id << n._name << n._last_seen << n._flags << n._addrs;
			}
		}
	}

	void read_archive(reader& r, heartbeat* hb) {
		bool state;
		hb->_nodes.clear();
		r >> hb->_name
		  >> hb->_flags
		  >> state;
		if (state) {
			int nnodes;
			r >> hb->_elector
			  >> hb->_master
			  >> hb->_cluster_maintenance
			  >> nnodes;
			// TODO: range-check nnodes
			for (int i = 0; i < nnodes; ++i) {
				heartbeat::node n;
				r >> n._id >> n._name >> n._last_seen >> n._flags >> n._addrs;
				hb->_nodes.push_back(n);
			}
		}
		else {
			hb->_elector = nil_uuid();
			hb->_master = nil_uuid();
		}
	}

	void write_archive(archive& a, const request* rq) {
		a << rq->_cmd
		  << rq->_args;
	}

	void read_archive(reader& r, request* rq) {
		r >> rq->_cmd
		  >> rq->_args;
	}

	void write_archive(archive& a, const response* rs) {
		FOREACH(const response::values::value_type& e, rs->_response) {
			a << e.first;
			apply_visitor(response_appender(a), e.second);
		}
	}

	void read_archive(reader& r, response* rs) {
		// todo: clean up
		archive::iterator i = r.get();
		string fieldname;
		for (; i != r.end(); ++i) {
			fieldname = i.get<string>();
			++i;
			switch (i.type()) {
			case SmallString:
			case String:
				rs->_response[fieldname] = i.get<string>();
				break;
			case Bool:
				rs->_response[fieldname] = i.get<bool>();
				break;
			case SmallInt:
			case Int:
			case Uint8:
			case Uint16:
				rs->_response[fieldname] = i.get<int>();
				break;
			case List: {
				vector<string> vals;
				i.contents(vals);
				rs->_response[fieldname] = vals;
			}break;
			case UUID: {
				rs->_response[fieldname] = i.get<boost::uuids::uuid>();
			} break;
			case NilUUID: {
				rs->_response[fieldname] = nil_uuid();
			} break;
			case PosixTime: {
				rs->_response[fieldname] = i.get<boost::posix_time::ptime>();
			} break;
			case RawData: {
				typedef std::vector<uint8_t> bytes;
				size_t len = i.size();
				if (len > MAX_MSG_LEN) {
					LOG_ERROR("Size error in raw data: %lu bytes for: %s",
					          (unsigned long)len,
					          fieldname.c_str());
					break;
				}
				else if (len == 0) {
					rs->_response[fieldname] = bytes();
				}
				else {
					rs->_response[fieldname] = bytes(i.body(), i.body() + len);
				}
			} break;
			default:
				LOG_ERROR("Can't handle data type: %s for field: %s",
				          i.type_name(),
				          fieldname.c_str());
				break;
			}
		}
	}

	template <typename Body>
	void read_message_body(message* msg, reader& r) {
		Body* b = new Body;
		msg->_body.reset(b);
		read_archive(r, b);
	}

	// this only works because koi is single-threaded..
	static static_vector<uint8_t, MAX_MSG_LEN*10> compression_buffer;
	void perhaps_compress(vector<uint8_t>& to, const archive& a);
	void perhaps_decompress(vector<uint8_t>& from);


	void perhaps_compress(vector<uint8_t>& to, const archive& a) {
		if (a.size() > COMPRESSION_THRESHOLD) {
			compression_buffer.resize_max();
			unsigned long outsize = compression_buffer.size();
			int zip_ret = mz_compress(&compression_buffer.front(), &outsize, a.data(), a.size());
			if ((zip_ret == MZ_OK) && (outsize < MAX_MSG_LEN - 4)) {
				if (debug_mode) {
					LOG_TRACE("compressed from %d to %d bytes", (int)a.size(), (int)outsize);
				}

				compression_buffer.resize(outsize);
				to.clear();
				const uint8_t h[] = {0x80, 0, 0, 0};
				to.insert(to.end(), h, h+4);
				to.insert(to.end(), compression_buffer.begin(), compression_buffer.end());
			}
			else {
				LOG_ERROR("Failed to compress message: %s (%lu > %d bytes).", (zip_ret==MZ_OK)?"":mz_error(zip_ret), (unsigned long)outsize, MAX_MSG_LEN);
				to.clear();
				to.insert(to.end(), a.data(), a.data() + a.size());
			}
		}
		else {
			to.clear();
			to.insert(to.end(), a.data(), a.data() + a.size());
		}
	}

	void perhaps_decompress(vector<uint8_t>& from) {
		if ((from[0] == 0x80) &&
		    (from[1] == 0) &&
		    (from[2] == 0) &&
		    (from[3] == 0)) {
			compression_buffer.resize_max();
			unsigned long decomp_size = compression_buffer.size();
			int zip_ret = mz_uncompress(&compression_buffer.front(), &decomp_size, &(from[4]), from.size()-4);
			if (zip_ret != MZ_OK) {
				throw msg_error("Failed to decompress message: %s", mz_error(zip_ret));
			}
			if (debug_mode) {
				LOG_TRACE("decompressed from %d to %d bytes", (int)from.size()-4, (int)decomp_size);
			}
			compression_buffer.resize(decomp_size);
			from.clear();
			from.insert(from.end(), compression_buffer.begin(), compression_buffer.end());
		}
	}
}

namespace koi {
	inline
	net::endpoint read_endpoint(const archive::iterator& i) {
		using namespace boost::asio::ip;
		size_t nbytes = i.size();
		const uint8_t* data = i.body();
		net::endpoint ep;
		if (nbytes == 18) {
			address_v6::bytes_type b;
			memcpy(b.begin(), data, 16);
			ep.address(address(address_v6(b)));
			ep.port((data[16]<<8)|data[17]);
		}
		else if (nbytes == 6) {
			address_v4::bytes_type b;
			memcpy(b.begin(), data, 4);
			ep.address(address(address_v4(b)));
			ep.port((data[4]<<8)|data[5]);
		}
		return ep;
	}

	archive& operator<<(archive& a, const net::endpoint& ep) {
		if (ep.address().is_v6()) {
			boost::asio::ip::address_v6::bytes_type b;
			b = ep.address().to_v6().to_bytes();
			uint8_t buf[18];
			memcpy(buf, b.begin(), 16);
			buf[16] = (ep.port()>>8)&0xff;
			buf[17] = ep.port()&0xff;
			a.append(buf, 18);
		}
		else {
			boost::asio::ip::address_v4::bytes_type b;
			b = ep.address().to_v4().to_bytes();
			uint8_t buf[6];
			memcpy(buf, b.begin(), 4);
			buf[4] = (ep.port()>>8)&0xff;
			buf[5] = ep.port()&0xff;
			a.append(buf, 6);
		}
		return a;
	}

	reader& operator>>(reader& r, net::endpoint& ep) {
		ep = read_endpoint(r.get());
		++r;
		return r;
	}

	archive& operator<<(archive& a, const mru<net::endpoint>& m) {
		std::vector<net::endpoint> addrs;
		for (auto i = m.begin(); i != m.end(); ++i) {
			addrs.push_back(*i);
		}
		a << addrs;
		return a;
	}

	reader& operator>>(reader& r, mru<net::endpoint>& m) {
		auto addrs = r.get();
		if (!addrs.is_list()) {
			throw archive_error("list expected", addrs.type_name(), addrs.size());
		}
		else {
			for (auto i = addrs.begin(); i != addrs.end(); ++i) {
				m.insert(read_endpoint(i));
			}
		}
		r.skip();
		return r;
	}

	namespace msg {
		bool encode(vector<uint8_t>& to, const message* msg, const string& pass) {
			try {
				to.resize(0);

				archive a;
				a << (int)koi::version
				  << msg->_seqnr
				  << (int)msg->_op
				  << msg->_cluster_id
				  << msg->_sender_uuid;

				switch(msg->_op) {
				case base::HealthReport:
					write_archive(a, msg->body<healthreport>());
					break;
				case base::StateUpdate:
					write_archive(a, msg->body<stateupdate>());
					break;
				case base::Request:
					write_archive(a, msg->body<request>());
					break;
				case base::Response:
					write_archive(a, msg->body<response>());
					break;
				case base::HeartBeat:
					write_archive(a, msg->body<heartbeat>());
					break;
				default:
					throw msg_error("Invalid message type: %d", msg->_op);
				};

				a.done();
				if (!a.valid())
					throw msg_error("%s", "Invalid archive");

				if (debug_mode) {
					string repr = a.to_string();
					LOG_TRACE("encode: %s [%d bytes]", repr.c_str(), a.size());
				}

				perhaps_compress(to, a);

				// pad to 4 byte interval
				bytevector_pad4(to);

				// generate nonce
				static boost::mt19937 random_engine;
				static boost::variate_generator<boost::mt19937&, boost::uniform_int<>> mkrandom(random_engine, boost::uniform_int<>(0, INT_MAX));
				int nonce = mkrandom();
				string nonces = pass + lexical_cast<string>(nonce);

				// encrypt w pass + nonce
				SHA1::digest pwd = hashpass(nonces.c_str());
				if (!crypto::encrypt(&to.front(), to.size(), pwd._data32, 5))
					throw msg_error("%s", "Failed to encrypt");

				append_nonce(to, nonce);

				if (to.size() > MAX_MSG_LEN)
					throw msg_error("length > max: %d > %d", (int)to.size(), MAX_MSG_LEN);

				return true;
			}
			catch (archive_error const& e) {
				LOG_ERROR("Failed to encode message into archive: %s (%s : %zu)", e.what(), e.type, e.size);
				return false;
			}
			catch (std::exception const& e) {
				LOG_ERROR("Encode error: %s", e.what());
				return false;
			}
		}

		bool decode(message* msg, vector<uint8_t>& from, const string& pass) {
			try {
				if (from.size() > MAX_MSG_LEN)
					throw msg_error("length > max: %d > %d", (int)from.size(), MAX_MSG_LEN);

				// drop nonce
				int nonce = extract_nonce(from);
				strfmt<64> nonces("%s%d", pass.c_str(), nonce);

				// decrypt
				SHA1::digest pwd = hashpass(nonces.c_str());
				if (!crypto::decrypt(&from.front(), from.size(), pwd._data32, 5))
					throw msg_error("Decrypt failed for size %d", (int)from.size());

				perhaps_decompress(from);

				archive a(from);
				reader r(a);

				if (!a.valid()) {
					string hd = a.hexdump();
					LOG_ERROR("Invalid archive: %s", hd.c_str());
					throw msg_error("%s", "Invalid archive");
				}

				if (debug_mode) {
					string repr = a.to_string();
					LOG_TRACE("decode: %s", repr.c_str());
				}

				r >> msg->_version;
				if (msg->_version != koi::version)
					throw msg_error("Version mismatch: our (%d) != msg (%d)", koi::version, msg->_version);

				r >> msg->_seqnr;

				int op;
				r >> op;
				if (!(op >= 0 && op < base::NumOps))
					throw msg_error("Unknown operation: %d", op);

				msg->_op = (base::Type)op;

				r >> msg->_cluster_id
				  >> msg->_sender_uuid;

				switch (msg->_op) {
				case base::HealthReport:
					read_message_body<healthreport>(msg, r);
					break;
				case base::StateUpdate:
					read_message_body<stateupdate>(msg, r);
					break;
				case base::Request:
					read_message_body<request>(msg, r);
					break;
				case base::Response:
					read_message_body<response>(msg, r);
					break;
				case base::HeartBeat:
					read_message_body<heartbeat>(msg, r);
					break;
				default:
					throw msg_error("Invalid message type: %d", msg->_op);
				}

				return true;
			}
			catch (archive_error const& e) {
				LOG_ERROR("Assert when unspooling archive: %s (%s : %zu)", e.what(), e.type, e.size);
				return false;
			}
			catch (std::exception const& e) {
				LOG_ERROR("Decode error: %s", e.what());
				return false;
			}
		}
	}
}
