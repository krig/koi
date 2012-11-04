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

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/asio.hpp>
#include <boost/shared_ptr.hpp>

using namespace std;
using namespace boost;
using boost::system::error_code;

#include "nexus.hpp"
#include "archive.hpp"
#include "masterstate.hpp"
#include "sequence.hpp"
#include "network.hpp"
#include "clusterstate.hpp"
#include "cluster.hpp"

namespace {
	using namespace koi;

	int signals[] = {
		SIGHUP,
		SIGINT,
		SIGTERM
	};

	const char* signame(int signum) {
		switch (signum) {
		case SIGHUP: return "SIGHUP";
		case SIGINT: return "SIGINT";
		case SIGTERM: return "SIGTERM";
		case SIGUSR1: return "SIGUSR1";
		default: return "SIG(UNKNOWN)";
		};
	}

	volatile sig_atomic_t interrupted = 0;
	volatile sig_atomic_t reload_config = 0;

	void shutdown_signal_handler(int signum) {
		LOG_TRACE("%s: Shutdown.", signame(signum));
		interrupted = 1;
	}

	void reconfig_signal_handler(int signum) {
		LOG_TRACE("%s: Reconfigure.", signame(signum));
		reload_config = 1;
	}

	void install_signal_handlers() {
		for (int sig = 0; sig < (int)ASIZE(signals); ++sig) {
			LOG_TRACE("Installing shutdown signal handler for signal: %s.", signame(signals[sig]));
			signal(signals[sig], shutdown_signal_handler);
		}
		LOG_TRACE("Installing reconfig signal handler for signal: SIGUSR1.");
		signal(SIGUSR1, reconfig_signal_handler);
	}
}

namespace koi {
	void force_reload_config() {
		reload_config = 1;
	}
}

// TODO:
// Reorganize completely.
// 1. Start a cluster instance.
// 2. When leadership is acquired, start an elector.
// 3. On startup or when config changes, if the node is
//    configured as a runner, start a runner instance.
// 4. If config changes, start/stop runner instances as needed.
// 5. If leadership is lost, stop elector instance.

namespace koi {
	static const int RestartNode = -0xbeef;

	bool reload(settings& cfg, const vector<string>& configs) {
		settings new_settings(cfg);
		if (!new_settings.read_config(configs)) {
			LOG_ERROR("Failed to read config.");
			return false;
		}
		cfg = new_settings;
		LOG_TRACE("Mainloop: Reloaded config.");
		return true;
	}

	int run(settings& cfg, const vector<string>& configs) {
		install_signal_handlers();
		int ret = 0;

		try {
			do {
				LOG_INFO("Starting IO Service...");
				ret = 0;

				net::io_service io;

				LOG_INFO("Starting Nexus...");
				nexus router(io, cfg);

				if (!router.init()) {
					LOG_ERROR("Failed to initialize node.");
					throw std::runtime_error("Failed to initialize node.");
				}

				LOG_INFO("Entering mainloop");
				while (!interrupted) {
					io.poll();

					router.update();

					usleep((useconds_t)cfg._mainloop_sleep_time);

					if (reload_config) {
						reload_config = 0;
						if (reload(cfg, configs)) {
							const bool trivial = router.settings_changed(cfg);
							if (!trivial || cfg._force_restart) {
								LOG_WARN("Configuration changes requires node restart.");
								ret = RestartNode;
								break;
							}
							else {
								LOG_INFO("Configuration changes applied without restart.");
							}
						}
					}
				}
				LOG_INFO("Exiting mainloop with status 0x%x", ret);

				io.stop();
			} while (ret == RestartNode);
		}
		catch (const archive_error& e) {
			LOG_ERROR("Archive read/write error: %s. Type: %s. Size: %zu.", e.what(), e.type, e.size);
			ret = 1;
		}
		catch (const std::runtime_error& e) {
			LOG_ERROR("Runtime error: %s", e.what());
			ret = 1;
		}
		catch (const std::exception& e) {
			LOG_ERROR("Fatal exception: %s", e.what());
			ret = 1;
		}


		LOG_INFO("><:;;x>");
		return ret;
	}

}
