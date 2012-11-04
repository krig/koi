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
#include <string>
#include <boost/asio.hpp>

namespace koi {
	namespace net {
		typedef boost::asio::io_service io_service;
		typedef boost::asio::ip::address ipaddr;
		typedef boost::asio::ip::udp::socket socket;
		typedef boost::asio::ip::udp::endpoint endpoint;

		inline bool is_multicast(const ipaddr& ip) {
			if (ip.is_v4()) {
				return ip.to_v4().is_multicast();
			}
			return ip.to_v6().is_multicast();
		}

		inline bool is_loopback(const ipaddr& ip) {
			if (ip.is_v4()) {
				return (ip.to_v4().to_ulong() & 0xFF000000) == 0x7F000000;
			}
			return ip.to_v6().is_loopback();
		}

		inline bool is_unspecified(const ipaddr& ip) {
			if (ip.is_v4()) {
				return ip.to_v4().to_ulong() == 0;
			}
			return ip.to_v6().is_unspecified();
		}
	}
}

