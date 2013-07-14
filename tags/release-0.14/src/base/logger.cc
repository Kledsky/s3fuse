/*
 * base/logger.cc
 * -------------------------------------------------------------------------
 * Definitions for s3::base::logger static members and init() method.
 * -------------------------------------------------------------------------
 *
 * Copyright (c) 2012, Tarick Bedeir.
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

#include <limits.h>

#include "logger.h"

using s3::base::logger;

// log all messages unless instructed otherwise
int logger::s_max_level = INT_MAX;

void logger::init(int max_level)
{
  s_max_level = max_level;

  openlog("s3fuse", 0, 0);
}