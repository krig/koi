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
#include "file.hpp"

namespace koi {
	size_t file::size() const {
		if (_size == 0) {
			long now = tell();
			if (now == -1)
				throw std::runtime_error("ftell() returned -1");
			fseek(_file, 0L, SEEK_END);
			long end = tell();
			if (end == -1)
				throw std::runtime_error("ftell() returned -1");
			fseek(_file, now, SEEK_SET);
			_size = (size_t)end;
		}
		return _size;
	}

	// Read entire file into memory
	// This won't open files that are too big.
	// Too big meaning > 1GB.
	static const ssize_t MAX_FILE_SIZE = 1024*1024*1024;

	string read_file(const char* fname) {
		file f(fname, "r");
		if (f.open()) {
			ssize_t sz = f.size();
			if (sz > 0 && sz <= MAX_FILE_SIZE) {
				std::vector<char> contents;
				contents.reserve(sz+1);
				size_t amt = f.read(contents, sz);
				contents.resize(sz+1);
				contents[amt] = 0;
				return string(&contents.front());
			}
		}
		return "";
	}
}
