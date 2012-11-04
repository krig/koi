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

#include <boost/tokenizer.hpp>
#include <boost/algorithm/string.hpp>

namespace koi {
	using std::string;
	typedef std::vector<string> stringvec;
	using namespace boost;

	template <typename It> inline
	string join(It const& b, It const& e, const char* w = "") {
		It i = b;
		if (i == e)
			return "";

		std::stringstream ss;
		ss << *i;
		++i;
		while (i != e) {
			ss << w << *i;
			++i;
		}
		return ss.str();
	}

	inline
	stringvec split(const string& s, const string& sep = " ") {
		stringvec ret;
		typedef char_separator<char> sep_t;
		typedef tokenizer<sep_t> tok_t;
		sep_t sp(sep.c_str());
		tok_t tok(s, sp);
		FOREACH(const auto& token, tok)
			ret.push_back(token);
		return ret;
	}

	inline
	bool endswith(const string& s, const char* end) {
		const size_t elen = strlen(end);
		if (s.length() <= elen)
			return false;
		return s.compare(s.length()-elen, elen, end) == 0;
	}

	// Replacements for str(n)cpy, str(n)cat
	// Rationale: http://byuu.org/articles/programming/strcpycat
	// length argument includes null-terminator
	// returns: strlen(target)
	inline
	unsigned strmcpy(char *target, const char *source, unsigned length) {
		const char *origin = target;
		if(length) { while(*source && --length) *target++ = *source++; *target = 0; }
		return target - origin;
	}

	// Replacements for str(n)cpy, str(n)cat
	// Rationale: http://byuu.org/articles/programming/strcpycat
	// length argument includes null-terminator
	// returns: strlen(target)
	inline
	unsigned strmcat(char *target, const char *source, unsigned length) {
		const char *origin = target;
		while(*target && length) target++, length--;
		return (target - origin) + strmcpy(target, source, length);
	}


	using boost::trim;
}
