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

#define KOI_CHECK_PRINTF(fmt, idx) __attribute__ ((format (printf, fmt, idx)))

namespace koi {
	namespace logging {
		enum Colors { RED = 91, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE };

		extern bool colors;

		enum ModeFlags {
			LogToConsole = 0x0001,
			LogToSyslog = 0x0002,
			LogToFile = 0x0004
		};

		enum LogLevels {
			Trace = 0,
			Info = 1,
			Warn = 2,
			Error = 3
		};

		void set_log_mode(int flags = LogToConsole);
		void set_log_level(LogLevels level);

		void cprintf(int color, const char* fmt, ...) KOI_CHECK_PRINTF(2, 3);

		struct log_context {
			log_context(const char* fil, int lin, const char* fun);
			void trace(const char* fmt, ...) KOI_CHECK_PRINTF(2, 3);
			void info(const char* fmt, ...) KOI_CHECK_PRINTF(2, 3);
			void warn(const char* fmt, ...) KOI_CHECK_PRINTF(2, 3);
			void error(const char* fmt, ...) KOI_CHECK_PRINTF(2, 3);

			const char* _file;
			int _line;
			const char* _fun;
		};


		struct logproxy {
			static const int PROXY_BUFSIZE = 4096;

			struct threadlocal {
				logproxy* proxy;
				int outpipe;
				char buf1[PROXY_BUFSIZE]; // space for two lines
				char buf2[PROXY_BUFSIZE];
			};

			logproxy();
			~logproxy();
			bool create();
			void close();
			void run(threadlocal* data);

			int inpipe;

			char* _consume(char* wptr, ssize_t amt, char*const buf1, char*const buf2);
			void _eject(const char* str);
		};

	}
}

#define LOG_TRACE koi::logging::log_context(__FILE__, __LINE__, __FUNCTION__).trace
#define LOG_INFO koi::logging::log_context(__FILE__, __LINE__, __FUNCTION__).info
#define LOG_WARN koi::logging::log_context(__FILE__, __LINE__, __FUNCTION__).warn
#define LOG_ERROR koi::logging::log_context(__FILE__, __LINE__, __FUNCTION__).error
