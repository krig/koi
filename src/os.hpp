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

namespace koi {
	namespace os {
		const char* environ(const char* var);
		void set_environ(const char* var, const char* val);

		namespace path {
			const char* basename(const char* full_path);
			const char* extension(const char* full_path);
			string path(const char* full_path);

			bool isdir(const char* fname);
			bool isexec(const char* fname);
			bool exists(const char* filename);

			bool makedir(const char* dir);
			bool makepath(const char* dir);

			inline const char* basename(const string& full_path) { return basename(full_path.c_str()); }
			inline const char* extension(const string& full_path) { return extension(full_path.c_str()); }
			inline string path(const string& full_path) { return path(full_path.c_str()); }

			inline bool isdir(const string& fname) { return isdir(fname.c_str()); }
			inline bool isexec(const string& fname) { return isexec(fname.c_str()); }
			inline bool exists(const string& filename) { return exists(filename.c_str()); }
		}
	}
}
