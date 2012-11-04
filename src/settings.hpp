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
    net::endpoint parse_endpoint(const char* str, uint16_t default_port);
    string to_string(const net::endpoint& ep);
    std::ostream & operator<<(std::ostream & o, const net::endpoint& ep);

    struct service_event {
        string _name;
        uint64_t _timeout;

        service_event(const char* name, uint64_t t);
    };

    static const int AUTO_RECOVER_MAX_FACTOR = 8;

    struct settings {
        typedef logging::LogLevels LogLevel;

        settings();
        bool  boot(std::vector<string> const& configs, bool verbose = true);
        bool  read_config(const std::vector<string>& configs, bool verbose = true);
        const service_event* svc(const char* name) const;

        // node
        ptime       _starttime;
        uuid        _uuid;
        string      _name;

        bool        _elector;
        bool        _runner;
        bool        _force_restart;

        uint16_t    _port;

        bool        _cluster_maintenance; // whole cluster maintenance mode
        uint64_t    _boot_count;
        bool        _reuse_address; // set SO_REUSEADDR option
        bool        _incremental_port; // use port+1 if port is busy (mostly good for runners)
        LogLevel    _loglevel; // trace / info / warn / error
        int         _cluster_id;
        int         _cluster_quorum; // if > 0, only promote (/stay promoted) if nnodes >= _cluster_quorum

        // cluster
        string        _pass;
        string        _transport;

        // services
        string        _services_folder;
        string        _services_workingdir;
        service_event _on_none;
        service_event _on_start;
        service_event _on_stop;
        service_event _on_status;
        service_event _on_promote;
        service_event _on_demote;
        service_event _on_failed;

        // timeouts and intervals
        // all in microseconds

        // how often the services are status checked
        uint64_t _status_interval;

        // how often the cluster is updated
        uint64_t _cluster_update_interval;

        // how often state changes are performed in the service manager
        uint64_t _state_update_interval;

        // how often the runner sends health reports
        uint64_t _runner_tick_interval;

        // how often the elector sends state updates
        uint64_t _elector_tick_interval;

        // how long before warning that the elector is lost
        uint64_t _runner_elector_lost_time;

        // how long before dropping to slave state due to missing elector
        uint64_t _runner_elector_gone_time;

        // how long before dropping to slave state due to lost quorum
        uint64_t _quorum_demote_time;

        // time to sleep between io updates
        uint64_t _mainloop_sleep_time;

        // how long before the master is declared dead and a new master is elected
        uint64_t _master_dead_time;

        // time that we ignore calls for promotion/demotion from a new elector (let it settle first)
        uint64_t _elector_startup_tolerance;

        // minimum uptime before an elector decides to promote a node to master
        uint64_t _elector_initial_promotion_delay;

        // if 0, don't auto recover
        // if > 0, attempt recovery N times before permanent failure
        uint64_t _service_auto_recover;

        // time before auto recovery is attempted (10s)
        uint64_t _auto_recover_time;

        // scaling factor, if > 1 recovery time is max(time, time*pow(count+1, factor))
        // time = 10, factor = 2 gives
        // failcount = 0: 10s
        // failcount = 1: 20s (so ~30s from initial failure)
        // failcount = 2: 40s (so ~70s from initial failure)
        int _auto_recover_wait_factor;

        // time that a runner has to run successfully before
        // failcount is reset (60*30s)
        uint64_t _service_failcount_reset_time;

        // time before a runner that has failed can be promoted (60s)
        uint64_t _runner_failure_promotion_timeout;
    };

}
