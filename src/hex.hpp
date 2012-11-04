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

#include <sstream>
#include <string>

namespace koi {
	inline int from_hex( char c ) {
		if ( '0' <= c && c <= '9' )
			return c - '0';
		if ( 'a' <= c && c <= 'f' )
			return c - 'a' + 10;
		if ( 'A' <= c && c <= 'F' )
			return c - 'A' + 10;
		return 0xff;
	}
	inline char from_hex( const char *c ) {
		return (char)(( from_hex( c[ 0 ] ) << 4 ) | from_hex( c[ 1 ] ));
	}

	inline string hexdump(const void* inRaw, int len) {
		static const char hexchars[] = "0123456789abcdef";

		std::stringstream out;
		auto in = reinterpret_cast<const char*>(inRaw);
		for (int i=0; i<len; ++i) {
			char c = in[i];
			char hi = hexchars[(c & 0xF0) >> 4];
			char lo = hexchars[(c & 0x0F)];

			out << hi << lo;
		}

		return out.str();
	}
}
