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
#include "os.hpp"

using namespace std;

namespace koi {
	namespace os {
		const char* environ(const char* var) {
			return getenv(var);
		}

		void set_environ(const char* var, const char* val) {
			setenv(var, val, true);
		}


		namespace path {
			const char* basename(const char* full_path) {
				const char* slash = strrchr(full_path, '/');

				if (slash && *(slash + 1))
					return slash + 1;

				return full_path;
			}

			const char* extension(const char* full_path) {
				const char* dot = strrchr(full_path, '.');

				if (dot && *(dot + 1))
					return dot + 1;

				return 0;
			}

			string path(const char* full_path) {
				if (isdir(full_path))
					return full_path;

				const char* base = basename(full_path);

				if (base == full_path)
					return full_path;

				if (base == 0)
					return "";

				// strip slash at end?
				return string(full_path, base - full_path);
			}

			bool isdir(const char* fname) {
				struct stat s;

				if (stat(fname, &s) == 0)
					return (S_ISDIR(s.st_mode) != 0);

				return false;
			}

			bool isexec(const char* fname) {
				struct stat s;

				if (stat(fname, &s) == 0) {
					if (S_ISREG(s.st_mode)) {
						if (s.st_mode & (S_IXUSR)) {
							return true;
						}
					}
					return false;
				}
				return false;
			}

			bool exists(const char* filename) {
				struct stat s;
				return ::stat(filename, &s) == 0;
			}


			bool makedir(const char* dir) {
				if (isdir(dir))
					return true;
				return mkdir(dir, 0755) == 0;
			}

			/*
			  namespace {
			  char* trim(char* str, char* chars) {
			  if (!str)
			  return 0;

			  char* end = strchr(str, 0) - 1;

			  while (end >= str && strchr(chars, *end)) {
			  *end = 0;
			  end--;
			  }

			  return str;
			  }
			  }
			*/

			bool makepath(const char* dir) {
				if (dir == 0)
					return false;

				string xpath(dir);

				if (exists(xpath.c_str())) {
					if (!isdir(xpath.c_str())) {
						LOG_ERROR("ERROR: trying to create dir '%s', but it's a file!", dir);
						return false;
					}

					return true;
				}

				// grab first part, create it if needed; move string pointer forward, loop
				string currpart;

				const char* at = xpath.c_str();

				while (*at) {
					const char* next = strchr(at, '/');

					if (next == at) {
						++at;
						continue;
					}

					if (next) {
						currpart.assign(xpath.c_str(), next - xpath.c_str());
						if (!makedir(currpart.c_str()))
							return false;
					}
					// last part!
					else {
						if (!makedir(xpath.c_str()))
							return false;
						break;
					}

					at = next + 1;
				}
				return true;
			}
		}
	}
}
