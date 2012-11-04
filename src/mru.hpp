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

#include <set>

namespace koi {
	template <typename T>
	struct mru {
		typedef typename std::set<T> values;
		typedef typename values::iterator const_iterator;
		typedef typename values::iterator iterator;

		mru() {
			_mru = _entries.end();
		}

		mru(const mru& other) : _entries(other._entries), _mru() {
			if (other._mru != other._entries.end()) {
				_mru = _entries.find(*other._mru);
			}
			else {
				_mru = _entries.end();
			}
		}

		mru& operator=(const mru& other) {
			if (this != &other) {
				_entries = other._entries;
				if (other._mru != other._entries.end()) {
					_mru = _entries.find(*other._mru);
				}
				else {
					_mru = _entries.end();
				}
			}
			return *this;
		}

		bool ok() const {
			return !_entries.empty();
		}

		const T& get() const {
			if (_mru == _entries.end())
				throw std::runtime_error("mru::get() on empty set");
			return *_mru;
		}

		void insert(const T& t) {
			auto ret = _entries.insert(t);
			_mru = ret.first;
		}

		iterator begin() {
			return _entries.begin();
		}

		const_iterator begin() const {
			return _entries.begin();
		}

		iterator end() {
			return _entries.end();
		}

		const_iterator end() const {
			return _entries.end();
		}

		bool operator==(const mru& other) const {
			return _entries == other._entries;
		}

		bool operator!=(const mru& other) const {
			return _entries != other._entries;
		}

		void merge(const mru& other) {
			_entries.insert(other.begin(), other.end());
			if (_mru == _entries.end()) {
				_mru = _entries.find(other.get());
			}
		}

		values _entries;
		iterator _mru;
	};

	template <typename T> inline
	std::ostream & operator<<(std::ostream & o, const mru<T>& m) {
		bool first = true;
		for (typename mru<T>::const_iterator i = m.begin(); i != m.end(); ++i) {
			if (first) {
				first = false;
			}
			else {
				o << ";";
			}
			o << *i;
		}
		return o;
	}

}
