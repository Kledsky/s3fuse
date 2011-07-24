/*
 * util.h
 * -------------------------------------------------------------------------
 * Various utility methods, e.g., MD5 digests, Base64 encoding, etc.
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 2011, Tarick Bedeir.
 * 
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef S3_UTIL_H
#define S3_UTIL_H

#include <stdint.h>
#include <stdio.h>

#include <fstream>
#include <string>

namespace s3
{
  enum md5_output_type
  {
    MOT_BASE64,
    MOT_HEX
  };

  class util
  {
  public:
    static double get_current_time();

    static std::string url_encode(const std::string &url);
    static std::string hex_encode(const uint8_t *input, size_t size);
    static std::string base64_encode(const uint8_t *input, size_t size);

    static std::string sign(const std::string &key, const std::string &data);

    static std::string compute_md5(int fd, md5_output_type type = MOT_BASE64, ssize_t size = 0, off_t offset = 0);
    static bool is_valid_md5(const std::string &md5);

    static void open_private_file(const std::string &file, std::ifstream *f, std::ios_base::openmode mode = static_cast<std::ios_base::openmode>(0));
    static void open_private_file(const std::string &file, std::ofstream *f, std::ios_base::openmode mode = std::ios_base::trunc);
  };
}

#endif
