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
#include "crypt.hpp"

using namespace std;

#define MX  ( (((z>>5)^(y<<2))+((y>>3)^(z<<4)))^((sum^y)+(key[(p&3)^e]^z)) )

namespace {
	void btea_encrypt(int32_t* v, int32_t length, int32_t* key) {
		const uint32_t DELTA = 0x9e3779b9;
		uint32_t z /* = v[length-1] */, y=v[0], sum=0, e;
		int32_t p, q ;

		/* Moved z=v[length-1] to here, else segmentation fault in decode when length < 0 */
		z=v[length-1];
		q = 6 + 52/length;
		while (q-- > 0) {
			sum += DELTA;
			e = (sum >> 2) & 3;
			for (p=0; p<length-1; p++) {
				y = v[p+1];
				z = v[p] += MX;
			}
			y = v[0];
			z = v[length-1] += MX;
		}
	}

	void btea_decrypt(int32_t* v, int32_t length, int32_t* key) {
		const uint32_t DELTA = 0x9e3779b9;
		uint32_t z /* = v[length-1] */, y=v[0], sum=0, e;
		int32_t p, q ;

		q = 6 + 52/length;
		sum = q*DELTA ;
		while (sum != 0) {
			e = (sum >> 2) & 3;
			for (p=length-1; p>0; p--) {
				z = v[p-1];
				y = v[p] -= MX;
			}
			z = v[length-1];
			y = v[0] -= MX;
			sum -= DELTA;
		}
	}

	void copyCharToInt(int32_t* tgt, const uint8_t* src, size_t len) {
		const size_t nints = len/4;
		const size_t trail = len%4;
		for (size_t i = 0; i < nints; ++i) {
			tgt[i] = src[i*4 + 0] + (src[i*4 + 1] << 8) + (src[i*4 + 2] << 16) + (src[i*4 + 3] << 24);
		}
		if (trail) {
			tgt[nints] = src[nints*4 + 0] + (src[nints*4 + 1] << 8) + (src[nints*4 + 1] << 16);
		}
	}

	void copyIntToChar(uint8_t* tgt, const int32_t* src, size_t len) {
		size_t nints = len/4;
		size_t trail = len%4;
		for (size_t i = 0; i < nints; ++i) {
			tgt[i*4 + 0] = src[i]&0xff;
			tgt[i*4 + 1] = (src[i]>>8)&0xff;
			tgt[i*4 + 2] = (src[i]>>16)&0xff;
			tgt[i*4 + 3] = (src[i]>>24)&0xff;
		}

		if (trail) {
			tgt[nints*4 + 0] = src[nints]&0xff;
			tgt[nints*4 + 1] = (src[nints]>>8)&0xff;
			tgt[nints*4 + 2] = (src[nints]>>16)&0xff;
			tgt[nints*4 + 3] = 0;
		}
	}
}

namespace koi {
	namespace crypto {
		bool encrypt(uint8_t* data, size_t len,
		             uint32_t* key, size_t keylen) {
			if (keylen < 4) {
				LOG_ERROR("key too short");
				return false;
			}
			if (len % 4) {
				LOG_ERROR("data buffer size not divisible by four: %lu", (unsigned long)len);
				return false;
			}

			if (len < 4) {
				LOG_ERROR("message to encrypt is too short: %lu", (unsigned long)len);
				return false;
			}

			union {
				uint32_t* ui;
				int32_t* ii;
			} keymap;
			keymap.ui = key;

			size_t len_4 = len/4;
			vector<int32_t> tmp(len_4, 0);
			copyCharToInt(&tmp.front(), data, len);
			btea_encrypt(&tmp.front(), (int32_t)len_4, keymap.ii);
			copyIntToChar(data, &tmp.front(), len);
			return true;
		}

		bool decrypt(uint8_t* data, size_t len,
		             uint32_t* key, size_t keylen) {
			if (keylen < 4) {
				LOG_ERROR("key too short");
				return false;
			}
			if (len % 4) {
				LOG_ERROR("data buffer size not divisible by four");
				return false;
			}
			if (len < 4) {
				LOG_ERROR("message to decrypt is too short");
				return false;
			}

			union {
				uint32_t* ui;
				int32_t* ii;
			} keymap;
			keymap.ui = key;

			vector<int32_t> tmp(len/4, 0);
			copyCharToInt(&tmp.front(), data, len);
			btea_decrypt(&tmp.front(), (int32_t)len/4, keymap.ii);
			copyIntToChar(data, &tmp.front(), len);
			return true;
		}
	}
}
