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

#include <typeinfo>
#include <boost/date_time/gregorian/conversion.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <exception>
#include "hex.hpp"

/* chive - a small serializer library
 * intended for serializing data to fit
 * in a single UDP packet.
 *
 * limitations:
 * maximum of 15 different types
 * (type 15 is be reserved for extensions
 * maximum of 255 transferrable types
 * maximum length of entire archive is 4k
 * ptimes are transferred as 64bit milliseconds,
 * so precision is millisecond only
 *
 * notes:
 * overhead per stored entry is 1 byte, plus
 * 2 bytes for the entire archive.
 * dynamic types like string, rawdata, list have
 * 2 bytes overhead
 * no map type, store as interleaved list (key value key value..)
 *
 * There is a wrapper class used when reading from an archive:
 *
 * archive a(data);
 * reader r(a);
 * r >> var1 >> var2 >> var3;
 *
 * Writing to the archive is done directly:
 *
 * archive a;
 * a << var1 << var2 << var3;
 *
 * To read unknown data from an archive, iterate it and
 * ask the iterator for type and size information, then
 * use the get<>() member function to parse the data:
 *
 * archive::iterator i = a.begin();
 * if (i.type() == chive::String)
 *     s = i.get<string>();
 */

namespace koi {
	using std::string;
	using boost::posix_time::ptime;
	using boost::uuids::uuid;

	enum BaseTypes {
		Null = 0,
		Bool,
		SmallInt, // <= 15
		Uint8,
		Uint16,
		Int,
		Uint64,
		PosixTime,
		UUID,
		NilUUID,
		String,
		SmallString,
		RawData,
		List, // max 4k
		BigList, // size=20 bits = 1Mb
		MaxType
	};

	struct archive_error : public std::runtime_error {
		archive_error(const char* msg, const char* t = "", size_t sz = 0)
			: std::runtime_error(msg), type(t), size(sz) {
		}
		const char* type;
		size_t size;
	};

	inline namespace internal {
		       static const int Number = SmallInt|Uint8|Uint16|Int;

		       inline bool is_fixed_size(uint8_t t) {
			       return t < String;
		       }

		       inline const char* type_name(uint8_t t) {
			       static const char* tn[] = {
				       "Null", "Bool",
				       "SmallInt", "Uint8", "Uint16",
				       "Int", "Uint64", "PosixTime",
				       "Uuid", "NilUuid", "String",
				       "SmallString", "RawData", "List", "BigList"
			       };
			       return (t < MaxType)? tn[t] : "Err";
		       }

		       inline size_t fixed_size(uint8_t t) {
			       switch (t) {
			       case Null:
			       case Bool:
			       case SmallInt:
			       case NilUUID:
				       return 0;
			       case Uint8:
				       return 1;
			       case Uint16:
				       return 2;
			       case Int:
				       return 4;
			       case Uint64:
			       case PosixTime:
				       return 8;
			       case UUID:
				       return 16;
			       default:
				       throw archive_error("not a fixed size", type_name(t), 0);
			       };
		       }

		       static const int overhead = 1;
	}

	struct chunk_iterator {
		chunk_iterator() : _pos(0), _end(0) {}
		chunk_iterator(const chunk_iterator& o) : _pos(o._pos), _end(o._end) {}
		chunk_iterator(const uint8_t* p, const uint8_t* e) : _pos(p), _end(e) {}

		chunk_iterator& operator=(const chunk_iterator& o) {
			_pos = o._pos;
			_end = o._end;
			return *this;
		}

		inline
		uint8_t type() const { return is_end() ? 0xff : (_pos[0] & 0xf); }
		inline
		const char* type_name() const { return internal::type_name(type()); }
		inline
		size_t size() const {
			if (is_fixed_size(type())) {
				return (uint16_t)fixed_size(type());
			} else if (type() == SmallString) {
				return (_pos[0] >> 4);
			} else if (type() == BigList) {
				return (_pos[0] >> 4) | (_pos[1]<<4) | (_pos[2]<<12);
			} else {
				return (_pos[0] >> 4) | (_pos[1]<<4);
			}
		}
		inline
		bool is_list() const { return (type() == List) || (type() == BigList); }
		inline
		bool is_end() const { return _pos >= _end; }
		inline
		const uint8_t* body() const {
			return _pos + overhead + data_offset();
		}
		inline
		size_t data_offset() const {
			const uint8_t t = type();
			return is_fixed_size(t) ? 0 : ((t == BigList) ? 2 : ((t == SmallString) ? 0 : 1));
		}

		bool operator==(const chunk_iterator& o) const {
			return (o.is_end() && is_end()) || (o._pos == _pos && o._end == _end);
		}

		bool operator!=(const chunk_iterator& o) const {
			return !(o.is_end() && is_end()) || (o._pos != _pos) || (o._end != _end);
		}

		chunk_iterator& operator++() {
			if (_pos < _end)
				_pos = body() + size();
			return *this;
		}

		chunk_iterator operator++(int) {
			chunk_iterator r(*this);
			if (_pos < _end)
				_pos = body() + size();
			return r;
		}

		chunk_iterator begin() const {
			if (!is_list())
				return end();
			return chunk_iterator(body(), body() + size());
		}

		chunk_iterator end() const {
			const uint8_t* e = body() + size();
			return chunk_iterator(e, e);
		}

		template <typename T>
		T get() const;

		template <typename T>
		void contents(std::vector<T>& out) const;

		const uint8_t* _pos;
		const uint8_t* _end;
	};

	template <> inline
	bool chunk_iterator::get() const {
		if (type() != Bool)
			throw archive_error("not a boolean", type_name(), size());
		return (_pos[0]&0xf0) != 0;
	}

	template <> inline
	int chunk_iterator::get() const {
		switch (type()) {
		case SmallInt:
			return (_pos[0]>>4)&0xf;
		case Uint8:
			return body()[0];
		case Uint16:
			return body()[0] | (body()[1]<<8);
		case Int:
			return body()[0] | (body()[1]<<8) | (body()[2]<<16) | (body()[3]<<24);
		default:
			throw archive_error("not an integer", type_name(), size());
		}
	}

	template <> inline
	uint32_t chunk_iterator::get() const {
		if (type() != Int) {
			return get<int>();
		}
		else {
			return body()[0] | (body()[1]<<8) | (body()[2]<<16) | (body()[3]<<24);
		}
	}

	template <> inline
	uint16_t chunk_iterator::get() const {
		return get<int>();
	}

	template <> inline
	uint8_t chunk_iterator::get() const {
		return get<int>();
	}

	template <> inline
	uint64_t chunk_iterator::get() const {
		if (type() != Uint64)
			throw archive_error("not a Uint64", type_name(), size());
		const uint8_t* b = body();
		uint32_t tlo = b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
		uint32_t thi = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
		return (((uint64_t)thi)<<32) | (uint64_t)tlo;
	}

	template <> inline
	uuid chunk_iterator::get() const {
		if (type() == NilUUID)
			return boost::uuids::nil_uuid();
		if (type() != UUID)
			throw archive_error("not an UUID", type_name(), size());
		uuid ret;
		memcpy(&ret, body(), 16);
		return ret;
	}

	template <> inline
	ptime chunk_iterator::get() const {
		if (type() != PosixTime)
			throw archive_error("not a PosixTime", type_name(), size());
		const uint8_t* b = body();
		uint32_t tlo = b[0] | (b[1]<<8) | (b[2]<<16) | (b[3]<<24);
		uint32_t thi = b[4] | (b[5]<<8) | (b[6]<<16) | (b[7]<<24);
		ptime start(boost::gregorian::date(1970,1,1));
		return start + boost::posix_time::milliseconds(((uint64_t)thi)<<32 | (uint64_t)tlo);
	}

	template <> inline
	string chunk_iterator::get() const {
		const int t = type();
		if (t != String && t != SmallString)
			throw archive_error("not a string", type_name(), size());
		size_t len = size();
		if (len == 0)
			return string();
		return string(body(), body()+len);
	}

	template <typename T> inline
	void chunk_iterator::contents(std::vector<T>& out) const {
		if (!is_list())
			throw archive_error("contents() called on not-a-list", type_name(), size());
		for (chunk_iterator i = begin(); i != end(); ++i) {
			out.push_back(i.get<T>());
		}
	}

	template <typename T> inline
	T chunk_iterator::get() const {
		T out;
		contents(out);
		return out;
	}

	BOOST_STATIC_ASSERT(sizeof(uint64_t) == 8);

	struct archive {
		typedef chunk_iterator       iterator;
		typedef std::vector<uint8_t> storage;
		typedef std::vector<size_t>  stack_t;

		archive() : _data() {
			_data.reserve(256);
			_push_big_container();
		}

		archive(const storage& d) : _data(d) {
		}

		void clear() {
			_data.clear();
			_list_stack.clear();
			_push_big_container();
		}

		void done() {
			_pop_container();
		}

		bool valid() const {
			// only run if the archive is done()
			if (_data.size() < (size_t)overhead + 1)
				return false;
			iterator root(&_data.front(), (&_data.front()) + _data.size());
			return root.size() + overhead + root.data_offset() <= _data.size();
		}

		iterator begin() const {
			iterator root(&_data.front(), (&_data.front()) + _data.size());
			return root.begin();
		}

		iterator end() const {
			iterator root(&_data.front(), (&_data.front()) + _data.size());
			return root.end();
		}

		const uint8_t* data() const { return &_data.front(); }
		int size() const { return (int)_data.size(); }

		string hexdump() const {
			return koi::hexdump(data(), size());
		}

		string to_string() const;

		void append_null() {
			_append_chunk(Null, 0);
		}

		void append(bool bval) {
			_append_chunk(Bool, bval?1:0);
		}

		void append_smallint(uint8_t val) {
			if (val > 0xf)
				throw archive_error("Cannot encode value > 15 in SmallInt");
			_append_chunk(SmallInt, val);
		}

		void append(uint8_t val) {
			_append_chunk(Uint8, 1);
			_data.push_back(val);
		}

		void append(uint16_t val) {
			_append_chunk(Uint16, 2);
			_data.push_back(val&0xff);
			_data.push_back((val>>8)&0xff);
		}

		void append(int ival) {
			if (ival >= 0 && ival <= 0xf) {
				append_smallint(ival);
			}
			else if ((ival > 0) && (ival <= 0xff)) {
				append((uint8_t)ival);
			}
			else if ((ival > 0) && (ival <= 0xffff)) {
				append((uint16_t)ival);
			}
			else {
				_append_chunk(Int, 4);
				_data.push_back(ival&0xff);
				_data.push_back((ival&0xff00)>>8);
				_data.push_back((ival&0xff0000)>>16);
				_data.push_back((ival&0xff000000)>>24);
			}
		}

		void append(uint32_t ival) {
			if (ival <= 0xf) {
				append_smallint(ival);
			}
			else if (ival <= 0xff) {
				append((uint8_t)ival);
			}
			else if (ival <= 0xffff) {
				append((uint16_t)ival);
			}
			else {
				_append_chunk(Int, 4);
				_data.push_back(ival&0xff);
				_data.push_back((ival&0xff00)>>8);
				_data.push_back((ival&0xff0000)>>16);
				_data.push_back((ival&0xff000000)>>24);
			}
		}

		void append(const std::string& str) {
			append(str.c_str());
		}

		void append(const char* str) {
			int len = (int)strlen(str);
			if (len <= 0xf) {
				_append_chunk(SmallString, len);
			}
			else {
				if (len > 0xfff)
					len = 0xfff;
				_append_chunk(String, len);
			}
			_data.insert(_data.end(), str, str+len);
		}

		void append(uint64_t val) {
			_append_chunk(Uint64, sizeof(uint64_t));
			uint32_t hibits = (uint32_t)((val>>32)&0xffffffff);
			uint32_t lobits = (uint32_t)(val&0xffffffff);
			_data.push_back(lobits&0xff);
			_data.push_back((lobits&0xff00)>>8);
			_data.push_back((lobits&0xff0000)>>16);
			_data.push_back((lobits&0xff000000)>>24);
			_data.push_back(hibits&0xff);
			_data.push_back((hibits&0xff00)>>8);
			_data.push_back((hibits&0xff0000)>>16);
			_data.push_back((hibits&0xff000000)>>24);
		}

		void append(const ptime& pt) {
			_append_chunk(PosixTime, sizeof(uint64_t));

			ptime start(boost::gregorian::date(1970,1,1));
			uint64_t millis = (pt - start).total_milliseconds();

			uint32_t hibits = (uint32_t)((millis>>32)&0xffffffff);
			uint32_t lobits = (uint32_t)(millis&0xffffffff);

			_data.push_back(lobits&0xff);
			_data.push_back((lobits&0xff00)>>8);
			_data.push_back((lobits&0xff0000)>>16);
			_data.push_back((lobits&0xff000000)>>24);
			_data.push_back(hibits&0xff);
			_data.push_back((hibits&0xff00)>>8);
			_data.push_back((hibits&0xff0000)>>16);
			_data.push_back((hibits&0xff000000)>>24);
		}

		void append(const uuid& uuid) {
			if (uuid.is_nil()) {
				_append_chunk(NilUUID, 0);
			}
			else {
				_append_chunk(UUID, 16);
				const uint8_t* ptr = (const uint8_t*)&uuid;
				_data.insert(_data.end(), ptr, ptr + 16);
			}
		}

		template <typename T>
		void append(const std::vector<T>& list) {
			_push_container();
			FOREACH(const auto& element, list)
				*this << element;
			_pop_container();
		}

		void append(const uint8_t* raw_data, size_t len) {
			if (len > 0xfff)
				throw archive_error("too much raw data for a tiny archive", "n/a", 0);
			_append_chunk(RawData, len);
			_data.insert(_data.end(), raw_data, raw_data + len);
		}

		inline size_t _append_chunk(uint8_t typ, size_t sz) {
			if (typ > 0xf)
				throw archive_error("type out of range!", "type", typ);
			if (sz > 0xfff)
				throw archive_error("size out of range!", "size", sz);
			size_t now = _data.size();
			_data.push_back((typ&0xf) | ((sz&0xf)<<4));
			if (!is_fixed_size(typ) && (typ != SmallString))
				_data.push_back((sz>>4)&0xff);
			if (typ == BigList)
				_data.push_back((sz>>12)&0xff);
			return now;
		}

		inline void _push_container() {
			_list_stack.push_back(_append_chunk(List, 0));
		}

		inline void _push_big_container() {
			_list_stack.push_back(_append_chunk(BigList, 0));
		}

		inline void _pop_container() {
			// backpatch the size of the list
			if (_list_stack.size() < 1)
				throw archive_error("container pop without matching push", "n/a", 0);
			const size_t indexof = _list_stack.back();
			_list_stack.pop_back();
			const size_t now = _data.size();
			const uint8_t typ = (uint8_t)(_data[indexof] & 0xf);
			size_t listtypesize = (typ==BigList)?2:1;
			size_t list_size = (now - indexof) - overhead - listtypesize;
			if ((int)(indexof + listtypesize) > (int)_data.size() - 1)
				throw archive_error("pointer arithmetic fuckup in pop_container", "n/a", 0);

			_data[indexof+0] = (typ&0xf) | ((list_size&0xf)<<4);
			_data[indexof+1] = (list_size>>4)&0xff;
			if (listtypesize > 1)
				_data[indexof+2] = (list_size>>12)&0xff;
		}

		storage _data;
		stack_t _list_stack;
	};

	struct reader {
		reader() : _archive(0), _i() {}
		reader(const archive& a) : _archive(&a), _i(a.begin()) {}

		void rewind() {
			_i = _archive->begin();
		}

		archive::iterator get() const {
			return _i;
		}

		void skip() {
			++_i;
		}

		reader& operator++() {
			++_i;
			return *this;
		}

		reader operator++(int) {
			reader r2;
			r2._archive = _archive;
			r2._i = _i;
			++_i;
			return r2;
		}

		archive::iterator end() const {
			return _archive->end();
		}

		template <typename T>
		T next() {
			T t = _i.get<T>();
			++_i;
			return t;
		}

		const archive* _archive;
		archive::iterator _i;
	};

	template <typename T>
	inline archive& operator<<(archive& a, const T& t) {
		a.append(t);
		return a;
	}

	template <typename T>
	inline reader& operator>>(reader& r, T& t) {
		t = r.next<T>();
		return r;
	}

	template <typename T>
	inline reader& operator>>(reader& r, std::vector<T>& v) {
		r.get().contents(v);
		r.skip();
		return r;
	}

	inline std::ostream& chunk_to_string(std::ostream& ss, const chunk_iterator& c, int depth);
	inline std::ostream& list_to_string(std::ostream& ss, const chunk_iterator& lst, int depth);

	inline std::ostream& chunk_to_string(std::ostream& ss, const chunk_iterator& c, int depth) {
		using namespace internal;
		using namespace boost;
		using namespace std;

		if (c.is_list())
			return list_to_string(ss, c, depth+1);

		switch (c.type()) {
		case Null: ss << "null"; break;
		case Bool: ss << (c.get<bool>() ? "true" : "false"); break;
		case SmallInt:
		case Uint8:
		case Uint16:
		case Int:
			ss << c.get<int>();
			break;
		case Uint64:
			ss << c.get<uint64_t>();
			break;
		case SmallString:
		case String:
			ss << c.get<string>();
			break;
		case PosixTime:
			ss << c.get<ptime>();
			break;
		case UUID:
			ss << c.get<uuid>();
			break;
		case NilUUID:
			ss << boost::uuids::nil_uuid();
			break;
		case RawData:
			ss << "<" << koi::hexdump(c.body(), (int)c.size()) << ">";
			break;
		default:
			ss << "unknown";
			break;
		};
		ss << " (" << c.type_name() << ":" << c.size() + overhead  + c.data_offset() << ")";

		return ss;
	}

	inline std::ostream& list_to_string(std::ostream& ss, const chunk_iterator& lst, int depth) {
		int breaker = 100;
		ss << "[";
		bool first = true;
		for (auto i = lst.begin(); i != lst.end(); --breaker, ++i) {
			if (!first) {
				ss << ",\n";
				for (int t = 0; t < depth; ++t)
					ss << "    ";
			}
			else {
				first = false;
			}
			chunk_to_string(ss, i, depth);
			if (breaker < 1)
				break;
		}
		ss << "](" << lst.size() + overhead + lst.data_offset() << ")";

		return ss;
	}

	inline std::ostream& operator << (std::ostream& os, const chunk_iterator& i) {
		return list_to_string(os, i, 1);
	}

	inline string archive::to_string() const {
		if (!valid()) {
			return "[invalid/incomplete]";
		}
		else {
			chunk_iterator root(&_data.front(), (&_data.front()) + _data.size());
			std::stringstream ss;
			ss << root;
			return ss.str();
		}
	}

}
