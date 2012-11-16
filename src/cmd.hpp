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
	struct command {
		command();

		bool is_active() const;
		void begin(char* const* argv, const char* workdir);
		void begin_grab_stdout(char* const* argv, const char* workdir);
		void begin_pipe_stdout_to(char* const* argv, const char* workdir, int inpipe);
		bool query_complete();
		bool wait();

		void fake_succeeded();

		// reads from child stdout
		ssize_t read_stdout(char* to, int bufsize);

		bool kill(int sig);
		bool termkill(size_t sleep_usec);

		int exitcode;
		pid_t pid;
		int stdout_pipe[2]; // pipe to read from process

		ptime started_at;
	};

	bool execute_command(char* const* argv, const char* workdir, int* exitcode);
	bool execute_command(char* const* argv, const char* workdir, int* exitcode, string& output);
}
