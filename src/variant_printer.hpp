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

struct variant_printer : public static_visitor<string> {
	string _key;
	variant_printer(const string& key) : static_visitor<string>(), _key(key) {
	}

	string operator()(const int& i) const {
		if (_key == "state")
			return state_to_string((koi::State)i);
		else if (_key == "target-action")
			return service_action_string((koi::ServiceAction)i);
		else
			return lexical_cast<string>(i);
	}
	string operator()(const string& s) const {
		return s;
	}
	string operator()(const bool& b) const {
		return b ? "true" : "false";
	}

	string operator()(const uuids::uuid& uuid) const {
		return lexical_cast<string>(uuid);
	}

	string operator()(const vector<string>& v) const {
		stringstream ss;
		ss << "[";
		int s = (int)v.size();
		for (int i=0;i<s-1;++i){
			ss << v[i] << ", ";
		}
		if (s)
			ss << v[s-1];
		ss << "]";
		return ss.str();
	}

	string operator()(const vector<uint8_t>& v) const {
		return hexdump(v.data(), (int)v.size());
	}

	string operator()(const boost::posix_time::ptime& pt) const {
		return lexical_cast<string>(pt);
	}
};

