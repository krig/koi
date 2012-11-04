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

#define KOI_DAEMONIZE 1

#if KOI_DAEMONIZE
#include <sys/types.h>
#include <fcntl.h>
#endif//KOI_DAEMONIZE

#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <map>
#include <boost/program_options.hpp>
#include "msg.hpp"
#include "file.hpp"

using namespace std;
using namespace boost;
using namespace koi;
using namespace boost::program_options;

extern const char* g_program_build;

namespace {
    void usage(string const& hlp) {
        printf("koi %08x (%s)\n\n"                    \
               "   >(>     ><>               ><'>\n"    \
               "             ><((\">\n"                 \
               "   ><(((*>\n"                           \
               "%s"                                     \
               "Services should be placed in %s.\n"     \
               "\n", koi::version, g_program_build, hlp.c_str(), KOI_CONFIG_SERVICES);
    }

#if KOI_DAEMONIZE
    void daemonize_now(const char* pidfile) {
        pid_t pid = fork();
        if (pid < 0) // fork failed
            exit(1);
        if (pid > 0) { // parent
            if (strlen(pidfile) != 0) {
                file pf(pidfile, "w");
                if (pf.open()) {
                    fprintf(pf._file, "%d\n", (int)pid);
                }
            }
            exit(0);
        }

        // clear file creation mode mask
        umask(0);

        // new SID for child process
        pid_t sid = setsid();
        if (sid < 0)
            exit(1);

        // set workdir so we don't occupy
        // any temporary filesystems
        if ((chdir("/")) < 0)
            exit(1);

        // don't talk to strangers
        freopen("/dev/null", "r", stdin);
        freopen("/dev/null", "w", stdout);
        freopen("/dev/null", "w", stderr);
    }
#endif//KOI_DAEMONIZE

    bool parse_commandline(int argc, char* argv[], vector<string>& configs, string& name) {
            options_description generic("Generic options");
            generic.add_options()
                ("version,v", "Print version string")
                ("help,h", "Produce help message")
                ("no-color,C", "Disable log color output")
#if KOI_DAEMONIZE
                ("daemonize", value<string>(), "Detach from parent process (arg: PID file)")
#endif
                ("log", value<string>()->default_value("console"), "Log targets (console, syslog)")
                ;

            options_description conf("Configuration options");

            string def_file(KOI_CONFIG_FILES);
            typedef vector<string> strings;

            conf.add_options()
                ("file,f", value<strings>()->default_value(configs, def_file), "Configuration file")
                ("name,n", value<string>(), "Set the name of this node")
                ("debug,d", "Debug mode");

            options_description cmdline_options("");
            cmdline_options.add(generic).add(conf);

            positional_options_description p;

            variables_map vm;
            store(command_line_parser(argc, argv).options(cmdline_options).positional(p).run(), vm);
            notify(vm);

            if (vm.count("help")) {
                stringstream ss;
                ss << cmdline_options << endl;
                usage(ss.str());
                return false;
            }

            if (vm.count("version")) {
                printf("koi %08x (%s)\n", koi::version, g_program_build);
                return false;
            }

            if (vm.count("debug")) {
                koi::debug_mode = true;
            }

            if (vm.count("name")) {
                name = vm["name"].as<string>();
            }

#if KOI_DAEMONIZE
            if (vm.count("daemonize")) {
                string pidfile = vm["daemonize"].as<string>();
                daemonize_now(pidfile.c_str());
            }
#endif

            int logmodes = 0;
            if (vm.count("log")) {
                string logs = vm["log"].as<string>();
                if (logs.find("syslog") != string::npos) {
                    logmodes |= logging::LogToSyslog;
                }
                if (logs.find("console") != string::npos) {
                    logmodes |= logging::LogToConsole;
                }
                if (logs.find("file") != string::npos) {
                    logmodes |= logging::LogToFile;
                }
            }
            else {
                logmodes = logging::LogToConsole;
            }

            logging::set_log_mode(logmodes);
            logging::colors = !vm.count("no-color");

            configs = vm["file"].as<vector<string>>();

            return true;
    }
}

int main(int argc, char* argv[]) {
    try {
        vector<string> configs = split(KOI_CONFIG_FILES, ";");
        string name("");

        if (!parse_commandline(argc, argv, configs, name))
            return 0;

        settings cfg;
        if (name != "")
            cfg._name = name;
        if (!cfg.boot(configs))
            return 1;
        return run(cfg, configs);
    }
    catch(const unknown_option& e) {
        fprintf(stderr, "Unknown option: %s. See -h or --help.\n", e.what());
        return 1;
    }
    catch(const too_many_positional_options_error& e) {
        fprintf(stderr, "Error: Illegal positional arguments. See -h or --help.\n");
        return 1;
    }
    catch(const std::exception& e) {
        fprintf(stderr, "Internal error: %s\n", e.what());
        return 1;
    }
}
