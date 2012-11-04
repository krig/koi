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

#define KOI_VERSION 4

#include <stdlib.h>
#include <stdint.h>
#include <string>
#include <map>
#include <vector>

#include <boost/uuid/uuid.hpp>

#include <boost/foreach.hpp>

//#include "config.hpp"
#include "net.hpp"
#include "logging.hpp"

#define KOI_CONFIG_ROOT "/etc/koi"
#define KOI_CONFIG_FILES "./koi.conf;/etc/koi/koi.conf"
#define KOI_CONFIG_SERVICES "/etc/koi/services"
#define KOI_VARLIB "/var/lib/koi"
#define KOI_DEFAULT_CLUSTER_PORT 42012

#define ASIZE(a) (sizeof(a)/sizeof((a)[0]))

#define FOREACH BOOST_FOREACH
#define REVERSE_FOREACH BOOST_REVERSE_FOREACH

/*
 * koi
 *
 * Things to look at first:
 *
 * emitter.hpp is the base implementation and foundation for both
 * elector.hpp and runner.hpp. msg.hpp implements the message types
 * that are sent over the network. cli.hpp contains all the specifics
 * for the command line interface.
 *
 * chive.hpp is a serializer/deserializer used to pack the messages
 * sent over the wire.
 *
 * settings.hpp implements the configuration file reading/writing and
 * also contains the general configuration and constants.
 *
 * servicemgr.hpp manages services, running and tracking service
 * calls.
 *
 * The following boost libraries are used:
 *
 * asio - for networking support.
 * regex - for parsing ipv4/ipv6 address:port pairs
 * date_time - for posix_time, all timing related functionality
 * uuid - for UUID generation
 * program_options - to parse command lines
 * property_tree - for parsing the configuration file
 * system - used by asio/date_time
 */

namespace koi {
	static const int version = KOI_VERSION;

	extern bool debug_mode;

	enum State {
		S_Failed, // not running / not voting / not runnable
		S_Disconnected,
		S_Stopped, // not running/not voting
		S_Offline, // running offline/not voting (not used yet)
		S_Slave, // running slave/not voting
		S_Master,// running master/not voting
		S_Elector, // voting but not executing services
		S_Other, // non-voting elector
		S_NumStates
	};

	enum RunnerMode {
		R_Passive, // Can only be slave
		R_Active, // Can become master
		R_NumModes
	};

	const char* state_to_string(State s);
	const char* mode_to_string(RunnerMode m);

	using std::string;
	using boost::posix_time::ptime;
	using boost::uuids::uuid;
	using boost::system::error_code;

	struct settings;

	int run(settings& settings, const std::vector<string>& configs);
}

namespace std {
	inline std::vector<std::string>& operator<<(std::vector<std::string>& v, const char* t) {
		v.push_back(t);
		return v;
	}
}

namespace koi {
	inline void bytevector_pad4(std::vector<uint8_t>& vec) {
		const size_t size = vec.size();
		if (size & 3)
			vec.resize((size & ~3) + 4, 0);
	}

	namespace units {
		static const uint64_t deci = UINT64_C(10);
		static const uint64_t centi = UINT64_C(100);
		static const uint64_t milli = UINT64_C(1000);
		static const uint64_t micro = UINT64_C(1000000);
		static const uint64_t nano = UINT64_C(1000000000);
		static const uint64_t pico = UINT64_C(1000000000000);
	}
}

#include "settings.hpp"

