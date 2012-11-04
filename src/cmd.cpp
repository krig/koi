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
#include "cmd.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

using namespace std;
using namespace boost;
using namespace boost::posix_time;

namespace {

	void childishellspawn(char* const* argv) {
		stringstream cmdline;
		char* const* arg = argv;
		while (*arg) {
			cmdline << *arg << " ";
			++arg;
		}
		fprintf(stderr, "Error: Failed to execute: %s", cmdline.str().c_str());
	}
}

namespace koi {

	command::command() {
		pid = 0;
		exitcode = 0;
		stdout_pipe[0] = -1;
		stdout_pipe[1] = -1;
		started_at = ptime(min_date_time);
	}

	void command::begin(char* const* argv, const char* workdir) {
		exitcode = 0;
		stdout_pipe[0] = -1;
		stdout_pipe[1] = -1;

		pid = fork();

		if (pid == 0) {
			if (chdir(workdir) == -1) {
				LOG_ERROR("ERROR: Unable to change to directory %s.",
				          workdir);
			}

			// now, ignore some signals from parent process
			signal(SIGUSR1, SIG_IGN);
			signal(SIGUSR2, SIG_IGN);

			execvp(argv[0], argv);

			// If we got here, it means the command didn't execute
			childishellspawn(argv);
			exit(-1);
		}
		else if (pid != -1) {
			started_at = microsec_clock::local_time();
		}
		else {
			LOG_ERROR("Unable to fork: %s", argv[0]);
		}
	}

	void command::begin_grab_stdout(char* const* argv, const char* workdir) {
		exitcode = 0;
		if (pipe(stdout_pipe) < 0) {
			LOG_ERROR("can't make pipe");
			exit(-1);
		}

		pid = fork();

		// Child.
		if (pid == 0) {
			if (chdir(workdir) == -1) {
				LOG_ERROR("ERROR: Unable to change to directory %s.",
				          workdir);
			}

			close(1); // Close current stdout.
			dup( stdout_pipe[1]); // Make stdout go to write end of pipe.

			close( stdout_pipe[0]);

			// now, ignore some signals from parent process
			signal(SIGUSR1, SIG_IGN);
			signal(SIGUSR2, SIG_IGN);

			execvp(argv[0], argv);

			// If we got here, it means the command didn't execute
			childishellspawn(argv);
			exit(-1);
		}
		// parent
		else if (pid != -1) {
			close(stdout_pipe[1]);
			started_at = microsec_clock::local_time();
		}
		else {
			LOG_ERROR("Unable to fork: %s", argv[0]);
		}
	}

	void command::begin_pipe_stdout_to(char* const* argv, const char* workdir, int inpipe) {
		exitcode = 0;
		if (inpipe <= 0) {
			begin(argv, workdir);
			return;
		}

		pid = fork();

		if (pid == 0) {
			if (chdir(workdir) == -1) {
				LOG_ERROR("ERROR: Unable to change to directory %s.",
				          workdir);
			}
			::close(1); // close current stdout
			::dup2(inpipe, 1); // stdout -> inpipe
			::dup2(inpipe, 2); // stderr -> inpipe

			// now, ignore some signals from parent process
			signal(SIGUSR1, SIG_IGN);
			signal(SIGUSR2, SIG_IGN);

			execvp(argv[0], argv);

			// If we got here, it means the command didn't execute
			childishellspawn(argv);
			exit(-1);
		}
		else if (pid != -1) {
			started_at = microsec_clock::local_time();
		}
		else {
			LOG_ERROR("Unable to fork: %s", argv[0]);
		}
	}

	ssize_t command::read_stdout(char* to, int bufsize) {
		return read(stdout_pipe[0], to, bufsize);
	}

	bool command::is_active() const {
		return pid != 0;
	}

	void command::fake_succeeded() {
		pid = 0;
		exitcode = 0;
	}

	bool command::query_complete() {
		if (!is_active())
			return true;

		int status = 0;
		bool done = waitpid(pid, &status, WNOHANG) == pid;
		if (done) {
			exitcode = WEXITSTATUS(status);
			pid = 0;
		}
		return done;
	}

	bool command::wait() {
		if (!is_active())
			return true;

		int status = 0;

		if (waitpid(pid, &status, 0) == pid) {
			exitcode = WEXITSTATUS(status);
			pid = 0;
			return true;
		}
		else {
			pid = 0;
			return false;
		}
	}

	bool command::kill(int sig) {
		if (!is_active())
			return true;
		return ::kill(pid, sig) == 0;
	}

	bool execute_command(char* const* argv, const char* workdir, int* exitcode) {
		command cmd;
		cmd.begin(argv, workdir);
		bool ok = cmd.wait();
		if (exitcode)
			*exitcode = cmd.exitcode;
		return ok;
	}

	bool execute_command(char* const* argv, const char* workdir, int* exitcode, string& output) {
		output = "";
		command cmd;
		cmd.begin_grab_stdout(argv, workdir);

		char tmp[513];
		ssize_t ret = 0;
		do {
			ret = cmd.read_stdout(tmp, sizeof(tmp)-1);
			if (ret > 0) {
				tmp[ret] = 0;
				output += tmp;
			}
		} while (ret > 0);

		bool ok = cmd.wait();
		if (exitcode)
			*exitcode = cmd.exitcode;
		return ok;
	}

}
