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

#undef LOG_TRACE
#undef LOG_INFO
#undef LOG_WARN
#undef LOG_ERROR

#include <stdarg.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#define KOI_CONTEXT koi::logging::log_context(__FILE__, __LINE__, __FUNCTION__)

// Stub is needed since pthreads
// don't know about C++ objects
extern "C" void* logproxy_run(void* p) {
	auto data = static_cast<koi::logging::logproxy::threadlocal*>(p);
	data->proxy->run(data);
	return 0;
}

namespace {
	bool _syslog_open = false;
	FILE* _logfile = 0;

	void syslog_stop() {
		if (_syslog_open) {
			closelog();
			_syslog_open = false;
		}
	}

	void syslog_write(int priority, const char* str, va_list arglist) {
		if (!_syslog_open) {
			openlog("koi", LOG_CONS, LOG_USER);
			_syslog_open = true;
		}
		::vsyslog (priority, str, arglist);
	}

	void logfile_close() {
		if (_logfile) {
			fclose(_logfile);
		}
	}

	void logfile_write(const char* fil, int lin, const char* str, va_list arglist) {
		if (!_logfile) {
			_logfile = fopen("koi.log", "w");
			atexit(logfile_close);
		}
		if (_logfile) {
			fprintf(_logfile, "%s(%d): ", fil, lin);
			vfprintf(_logfile, str, arglist);
			fprintf(_logfile, "\n");
		}
	}
}

namespace koi {
	namespace logging {
		bool colors = true;
		int modeflags = LogToConsole;
		LogLevels loglevel = Trace;

		pthread_mutex_t _log_mutex = PTHREAD_MUTEX_INITIALIZER;

		struct log_lock {
			log_lock() {
				pthread_mutex_lock(&_log_mutex);
			}
			~log_lock() {
				pthread_mutex_unlock(&_log_mutex);
			}
		};

		void set_log_mode(int flags) {
			log_lock ll;
			modeflags = flags;
			if (!(modeflags & LogToConsole)) {
				syslog_stop();
			}
			if (!(modeflags & LogToFile)) {
				logfile_close();
			}
		}

		void set_log_level(LogLevels level) {
			loglevel = level;
		}


		logproxy::logproxy() {
			inpipe = -1;
		}

		logproxy::~logproxy() {
			close();
		}

		// log context has been lost at this point
		void logproxy::_eject(const char* str) {
			log_context("", -1, "").info("%s", str);
		}

		// copy complete lines from buf1 to buf2,
		// then write the completed lines to the log
		char* logproxy::_consume(char* wptr, ssize_t amt, char*const buf1, char*const buf2) {
			char* rptr = buf1;
			while ((rptr - buf1) < amt) {
				char next = *rptr;
				if (next == '\n') {
					if (wptr > buf2) {
						*wptr = 0;
						_eject(buf2);
					}
					wptr = buf2;
				}
				else {
					if (wptr >= buf2 + PROXY_BUFSIZE - 1) {
						*wptr = 0;
						_eject(buf2);
						wptr = buf2;
					}
					*wptr++ = next;
				}
				++rptr;
			}
			return wptr;
		}

		// loop, reading from the given end of the pipe until it is closed,
		// writing every line we get to the log
		void logproxy::run(logproxy::threadlocal* data) {
			char* wptr = data->buf2;
			for (;;) {
				ssize_t amt = read(data->outpipe, data->buf1, PROXY_BUFSIZE);
				if (amt <= 0) {
					if (amt < 0) {
						KOI_CONTEXT.error("Error in read() on fd %d. %s", data->outpipe, strerror(errno));
					}
					break;
				}

				wptr = _consume(wptr, amt, data->buf1, data->buf2);
			}
			::close(data->outpipe);
			delete data;
		}

		// Create a pipe to a thread that writes
		// everything it gets to the log. It can
		// then be passed to child processes to replace
		// stdout/stderr.
		bool logproxy::create() {
			int pip[2];

			close();

			if (pipe(pip) == -1) {
				KOI_CONTEXT.error("Failed to create pipe for subprocess logging.");
				return false;
			}

			inpipe = pip[1];

			threadlocal* data = new threadlocal;
			data->proxy = this;
			data->outpipe = pip[0];
			memset(data->buf1, 0, PROXY_BUFSIZE);
			memset(data->buf2, 0, PROXY_BUFSIZE);

			pthread_t proxythread;
			pthread_attr_t detach;
			pthread_attr_init(&detach);
			pthread_attr_setdetachstate(&detach, PTHREAD_CREATE_DETACHED);
			pthread_create(&proxythread, &detach, &logproxy_run, data);
			pthread_attr_destroy(&detach);

			return true;
		}

		void logproxy::close() {
			if (inpipe != -1) {
				::close(inpipe);
				inpipe = -1;
			}
		}

		namespace {
			const char* timenowstring(char* buf) {
				time_t t;
				struct tm tid;
				::time(&t);
				::localtime_r(&t, &tid);
				strftime (buf, 30, "%x %X", &tid);
				return buf;
			}
		}

		log_context::log_context(const char* fil, int lin, const char* fun) : _file(fil), _line(lin), _fun(fun) {
			static const char* prefixes[] = {
				"../src/",
				"src/"
			};
			for (size_t i = 0; i < ASIZE(prefixes); ++i)
				if (strncmp(_file, prefixes[i], strlen(prefixes[i])) == 0)
					_file += strlen(prefixes[i]);
		}

		void log_context::info(const char* fmt, ...) {
			if (loglevel > Info)
				return;

			log_lock ll;

			if (modeflags & LogToSyslog) {
				va_list va_args;
				va_start(va_args, fmt);
				syslog_write(LOG_INFO, fmt, va_args);
				va_end(va_args);
			}

			if (modeflags & LogToConsole) {
				char tmp[2048];
				va_list va_args;
				va_start(va_args, fmt);
				vsnprintf(tmp, sizeof(tmp), fmt, va_args);
				va_end(va_args);

				char tbuf[30];
				if (_line >= 0)
					fprintf(stdout, "%s %s(%d): %s\n", timenowstring(tbuf), _file, _line, tmp);
				else
					fprintf(stdout, "%s %s\n", timenowstring(tbuf), tmp);

				fflush(stdout);
			}

			if (modeflags & LogToFile) {
				va_list va_args;
				va_start(va_args, fmt);
				logfile_write(_file, _line, fmt, va_args);
				va_end(va_args);
			}
		}

		void log_context::trace(const char* fmt, ...) {
			if (loglevel > Trace)
				return;

			log_lock ll;

			if (modeflags & LogToSyslog) {
				va_list va_args;
				va_start(va_args, fmt);
				syslog_write(LOG_DEBUG, fmt, va_args);
				va_end(va_args);
			}

			if (modeflags & LogToConsole) {
				char tmp[2048];
				va_list va_args;
				va_start(va_args, fmt);
				vsnprintf(tmp, sizeof(tmp), fmt, va_args);
				va_end(va_args);

				char tbuf[30];
				if (colors)
					fprintf(stdout, "\e[%dm%s %s(%d): %s\e[0m\n", CYAN, timenowstring(tbuf), _file, _line, tmp);
				else
					fprintf(stdout, "%s %s(%d): %s\n", timenowstring(tbuf), _file, _line, tmp);
			}

			if (modeflags & LogToFile) {
				va_list va_args;
				va_start(va_args, fmt);
				logfile_write(_file, _line, fmt, va_args);
				va_end(va_args);
			}
		}

		void log_context::warn(const char* fmt, ...) {
			if (loglevel > Warn)
				return;

			log_lock ll;

			if (modeflags & LogToSyslog) {
				va_list va_args;
				va_start(va_args, fmt);
				syslog_write(LOG_WARNING, fmt, va_args);
				va_end(va_args);
			}

			if (modeflags & LogToConsole) {
				char tmp[2048];
				va_list va_args;
				va_start(va_args, fmt);
				vsnprintf(tmp, sizeof(tmp), fmt, va_args);
				va_end(va_args);

				char tbuf[30];
				if (colors)
					fprintf(stdout, "\e[%dm%s %s(%d): %s\e[0m\n", YELLOW, timenowstring(tbuf), _file, _line, tmp);
				else
					fprintf(stdout, "%s %s(%d): %s\n", timenowstring(tbuf), _file, _line, tmp);

				fflush(stdout);
			}

			if (modeflags & LogToFile) {
				va_list va_args;
				va_start(va_args, fmt);
				logfile_write(_file, _line, fmt, va_args);
				va_end(va_args);
			}
		}

		void log_context::error(const char* fmt, ...) {
			log_lock ll;

			if (modeflags & LogToSyslog) {
				va_list va_args;
				va_start(va_args, fmt);
				syslog_write(LOG_ERR, fmt, va_args);
				va_end(va_args);
			}

			if (modeflags & LogToConsole) {
				char tmp[2048];
				va_list va_args;
				va_start(va_args, fmt);
				vsnprintf(tmp, sizeof(tmp), fmt, va_args);
				va_end(va_args);

				char tbuf[30];
				if (colors)
					fprintf(stdout, "\e[%dm%s %s(%d): %s\e[0m\n", RED, timenowstring(tbuf), _file, _line, tmp);
				else
					fprintf(stdout, "%s %s(%d): %s\n", timenowstring(tbuf), _file, _line, tmp);

				fflush(stdout);
			}

			if (modeflags & LogToFile) {
				va_list va_args;
				va_start(va_args, fmt);
				logfile_write(_file, _line, fmt, va_args);
				va_end(va_args);
			}
		}

		void cprintf(int color, const char* fmt, ...) {
			char tmp[2048];
			va_list va_args;
			va_start(va_args, fmt);
			vsnprintf(tmp, sizeof(tmp), fmt, va_args);
			va_end(va_args);

			if (colors) {
				size_t len = strlen(tmp);
				bool nl = (tmp[len-1] == '\n');
				if (nl)
					tmp[len-1] = '\0';
				printf("\e[%dm%s\e[0m%s", color, tmp, nl?"\n":"");
			}
			else
				printf("%s", tmp);
		}
	}
}

