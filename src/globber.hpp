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

#include <glob.h>

namespace koi {
	struct globber {
		globber() {
			memset(&_glob, 0, sizeof(glob_t));
		}

		globber(const char* pattern) {
			memset(&_glob, 0, sizeof(glob_t));
			start(pattern);
		}

		~globber() {
			clear();
		}

		void start(const char* pattern) {
			glob(pattern, GLOB_TILDE|GLOB_NOSORT, 0, &_glob);
		}

		void append(const char* pattern) {
			glob(pattern, GLOB_TILDE|GLOB_NOSORT|GLOB_APPEND, 0, &_glob);
		}

		void clear() {
			if (_glob.gl_pathc) {
				globfree(&_glob);
				memset(&_glob, 0, sizeof(glob_t));
			}
		}

		size_t size() const {
			return _glob.gl_pathc;
		}

		const char* operator[](size_t index) const {
			return (index < _glob.gl_pathc)? _glob.gl_pathv[index] : "";
		}

		glob_t _glob;
	};

}
