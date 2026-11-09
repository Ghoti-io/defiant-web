/*
 * SPDX-License-Identifier: LGPL-3.0-only
 *
 * Copyright (C) 2026 Corey Pennycuff
 *
 * This file is part of Ghoti.io Defiant Web.
 *
 * Ghoti.io Defiant Web is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * Ghoti.io Defiant Web is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * mkdtemp() for the tests, on a platform whose C runtime has none, and the
 * system temporary directory to make the template in.
 */

#ifndef GDEFWEB_TESTS_PORTABLE_H
#define GDEFWEB_TESTS_PORTABLE_H

#include <stdlib.h>
#include <string.h>

#include <filesystem>
#include <random>
#include <string>
#include <system_error>

/** The system temporary directory, with no trailing separator. */
inline std::string PortableTempDir() {
  std::error_code ec;
  std::filesystem::path p = std::filesystem::temp_directory_path(ec);
  return ec ? std::string(".") : p.string();
}

#ifdef _WIN32
/* Fill the trailing six X's with digits and make the directory. */
inline char * mkdtemp(char * tmpl) {
  std::random_device entropy;
  size_t n = strlen(tmpl);
  if (n < 6 || strcmp(tmpl + n - 6, "XXXXXX") != 0) {
    return NULL;
  }
  for (int attempt = 0; attempt < 100; attempt++) {
    std::string digits = std::to_string(100000 + entropy() % 900000);
    std::error_code ec;
    memcpy(tmpl + n - 6, digits.data(), 6);
    if (std::filesystem::create_directory(tmpl, ec) && !ec) {
      return tmpl;
    }
    memcpy(tmpl + n - 6, "XXXXXX", 6);
  }
  return NULL;
}
#endif /* _WIN32 */

#endif /* GDEFWEB_TESTS_PORTABLE_H */
