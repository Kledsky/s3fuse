/*
 * main.cc
 * -------------------------------------------------------------------------
 * FUSE driver for S3.
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

#define FUSE_USE_VERSION 26

#include <errno.h>
#include <fuse.h>
#include <getopt.h>
#include <stdlib.h>
#include <string.h>

#include <limits>
#include <string>
#include <boost/detail/atomic_count.hpp>

#include "base/config.h"
#include "base/logger.h"
#include "base/statistics.h"
#include "base/timer.h"
#include "base/version.h"
#include "base/xml.h"
#include "fs/cache.h"
#include "fs/directory.h"
#include "fs/encrypted_file.h"
#include "fs/encryption.h"
#include "fs/file.h"
#include "fs/symlink.h"
#include "services/service.h"

using boost::static_pointer_cast;
using boost::detail::atomic_count;
using std::numeric_limits;
using std::ostream;
using std::string;
using std::vector;

using s3::base::config;
using s3::base::logger;
using s3::base::statistics;
using s3::base::timer;
using s3::base::xml;
using s3::fs::cache;
using s3::fs::directory;
using s3::fs::encrypted_file;
using s3::fs::encryption;
using s3::fs::file;
using s3::fs::object;
using s3::fs::symlink;
using s3::services::service;
using s3::threads::pool;

// also adjust path by skipping leading slash
#define ASSERT_VALID_PATH(str) \
  do { \
    if ((str)[0] != '/' || ((str)[1] != '\0' && (str)[strlen(str) - 1] == '/')) { \
      S3_LOG(LOG_WARNING, "ASSERT_VALID_PATH", "failed on [%s]\n", static_cast<const char *>(str)); \
      return -EINVAL; \
    } \
    \
    (str)++; \
  } while (0)

#define BEGIN_TRY \
  try {

#define END_TRY \
  } catch (const std::exception &e) { \
    S3_LOG(LOG_WARNING, "END_TRY", "caught exception: %s (at line %i)\n", e.what(), __LINE__); \
    return -ECANCELED; \
  } catch (...) { \
    S3_LOG(LOG_WARNING, "END_TRY", "caught unknown exception (at line %i)\n", __LINE__); \
    return -ECANCELED; \
  }

#define GET_OBJECT(var, path) \
  object::ptr var = cache::get(path); \
  \
  if (!var) \
    return -ENOENT;

#define GET_OBJECT_AS(type, mode, var, path) \
  type::ptr var = static_pointer_cast<type>(cache::get(path)); \
  \
  if (!var) \
    return -ENOENT; \
  \
  if (var->get_type() != (mode)) { \
    S3_LOG( \
      LOG_WARNING, \
      "GET_OBJECT_AS", \
      "could not get [%s] as type [%s] (requested mode %i, reported mode %i, at line %i)\n", \
      static_cast<const char *>(path), \
      #type, \
      mode, \
      var->get_type(), \
      __LINE__); \
    \
    return -EINVAL; \
  }

namespace
{
  struct options
  {
    const char *arg0;
    string config;
    string mountpoint;
    string stats;
    int verbosity;
    int mountpoint_mode;

    #ifdef __APPLE__
      bool noappledouble_set;
      bool daemon_timeout_set;
    #endif
  };

  atomic_count s_reopen_attempts(0), s_reopen_rescues(0), s_reopen_fails(0);
  atomic_count s_create(0), s_mkdir(0), s_open(0), s_rename(0), s_symlink(0), s_unlink(0);
  atomic_count s_getattr(0), s_readdir(0), s_readlink(0);

  void dir_filler(fuse_fill_dir_t filler, void *buf, const std::string &path)
  {
    filler(buf, path.c_str(), NULL, 0);
  }

  void statistics_writer(ostream *o)
  {
    *o <<
      "main (exceptions):\n"
      "  reopen attempts: " << s_reopen_attempts << "\n"
      "  reopens rescued: " << s_reopen_rescues << "\n"
      "  reopens failed: " << s_reopen_fails << "\n"
      "main (write):\n"
      "  create: " << s_create << "\n"
      "  mkdir: " << s_mkdir << "\n"
      "  open: " << s_open << "\n"
      "  rename: " << s_rename << "\n"
      "  symlink: " << s_symlink << "\n"
      "  unlink: " << s_unlink << "\n"
      "main (read):\n"
      "  getattr: " << s_getattr << "\n"
      "  readdir: " << s_readdir << "\n"
      "  readlink: " << s_readlink << "\n";
  }

  options s_opts;
  statistics::writers::entry s_entry(statistics_writer, 0);
}

int s3fuse_chmod(const char *path, mode_t mode)
{
  S3_LOG(LOG_DEBUG, "chmod", "path: %s, mode: %i\n", path, mode);

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    obj->set_mode(mode);

    return obj->commit();
  END_TRY;
}

int s3fuse_chown(const char *path, uid_t uid, gid_t gid)
{
  S3_LOG(LOG_DEBUG, "chown", "path: %s, user: %i, group: %i\n", path, uid, gid);

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    if (uid != static_cast<uid_t>(-1))
      obj->set_uid(uid);

    if (gid != static_cast<gid_t>(-1))
      obj->set_gid(gid);

    return obj->commit();
  END_TRY;
}

int s3fuse_create(const char *path, mode_t mode, fuse_file_info *file_info)
{
  int r, last_error = 0;
  const fuse_context *ctx = fuse_get_context();

  S3_LOG(LOG_DEBUG, "create", "path: %s, mode: %#o\n", path, mode);
  ++s_create;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    file::ptr f;

    if (cache::get(path)) {
      S3_LOG(LOG_WARNING, "create", "attempt to overwrite object at [%s]\n", path);
      return -EEXIST;
    }

    directory::invalidate_parent(path);

    if (config::get_use_encryption() && config::get_encrypt_new_files())
      f.reset(new encrypted_file(path));
    else
      f.reset(new file(path));

    f->set_mode(mode);
    f->set_uid(ctx->uid);
    f->set_gid(ctx->gid);

    r = f->commit();

    if (r)
      return r;

    // rarely, the newly created file won't be downloadable right away, so
    // try a few times before giving up.
    for (int i = 0; i < config::get_max_inconsistent_state_retries(); i++) {
      last_error = r;
      r = file::open(static_cast<string>(path), s3::fs::OPEN_DEFAULT, &file_info->fh);

      if (r != -ENOENT)
        break;

      S3_LOG(LOG_WARNING, "create", "retrying open on [%s] because of error %i\n", path, r);
      ++s_reopen_attempts;

      // sleep a bit instead of retrying more times than necessary
      timer::sleep(i + 1);
    }

    if (!r && last_error == -ENOENT)
      ++s_reopen_rescues;

    if (r == -ENOENT)
      ++s_reopen_fails;

    return r;
  END_TRY;
}

int s3fuse_flush(const char *path, fuse_file_info *file_info)
{
  file *f = file::from_handle(file_info->fh);

  S3_LOG(LOG_DEBUG, "flush", "path: %s\n", f->get_path().c_str());

  BEGIN_TRY;
    return f->flush();
  END_TRY;
}

int s3fuse_ftruncate(const char *path, off_t offset, fuse_file_info *file_info)
{
  file *f = file::from_handle(file_info->fh);

  S3_LOG(LOG_DEBUG, "ftruncate", "path: %s, offset: %ji\n", f->get_path().c_str(), static_cast<intmax_t>(offset));

  BEGIN_TRY;
    return f->truncate(offset);
  END_TRY;
}

int s3fuse_getattr(const char *path, struct stat *s)
{
  ASSERT_VALID_PATH(path);

  ++s_getattr;

  memset(s, 0, sizeof(*s));

  if (path[0] == '\0') { // root path
    s->st_uid = geteuid();
    s->st_gid = getegid();
    s->st_mode = s_opts.mountpoint_mode;
    s->st_nlink = 1; // because calculating nlink is hard! (see FUSE FAQ)

    return 0;
  }

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    obj->copy_stat(s);

    return 0;
  END_TRY;
}

#ifdef __APPLE__
int s3fuse_getxattr(const char *path, const char *name, char *buffer, size_t max_size, uint32_t position)
#else
int s3fuse_getxattr(const char *path, const char *name, char *buffer, size_t max_size)
#endif
{
  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    return obj->get_metadata(name, buffer, max_size);
  END_TRY;
}

int s3fuse_listxattr(const char *path, char *buffer, size_t size)
{
  typedef vector<string> str_vec;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    str_vec attrs;
    size_t required_size = 0;

    obj->get_metadata_keys(&attrs);

    for (str_vec::const_iterator itor = attrs.begin(); itor != attrs.end(); ++itor)
      required_size += itor->size() + 1;

    if (buffer == NULL || size == 0)
      return required_size;

    if (required_size > size)
      return -ERANGE;

    for (str_vec::const_iterator itor = attrs.begin(); itor != attrs.end(); ++itor) {
      strcpy(buffer, itor->c_str());
      buffer += itor->size() + 1;
    }

    return required_size;
  END_TRY;
}

int s3fuse_mkdir(const char *path, mode_t mode)
{
  const fuse_context *ctx = fuse_get_context();

  S3_LOG(LOG_DEBUG, "mkdir", "path: %s, mode: %#o\n", path, mode);
  ++s_mkdir;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    directory::ptr dir;

    if (cache::get(path)) {
      S3_LOG(LOG_WARNING, "mkdir", "attempt to overwrite object at [%s]\n", path);
      return -EEXIST;
    }

    directory::invalidate_parent(path);

    dir.reset(new directory(path));

    dir->set_mode(mode);
    dir->set_uid(ctx->uid);
    dir->set_gid(ctx->gid);

    return dir->commit();
  END_TRY;
}

int s3fuse_open(const char *path, fuse_file_info *file_info)
{
  S3_LOG(LOG_DEBUG, "open", "path: %s\n", path);
  ++s_open;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    return file::open(
      static_cast<string>(path), 
      (file_info->flags & O_TRUNC) ? s3::fs::OPEN_TRUNCATE_TO_ZERO : s3::fs::OPEN_DEFAULT, 
      &file_info->fh);
  END_TRY;
}

int s3fuse_read(const char *path, char *buffer, size_t size, off_t offset, fuse_file_info *file_info)
{
  BEGIN_TRY;
    return file::from_handle(file_info->fh)->read(buffer, size, offset);
  END_TRY;
}

int s3fuse_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t, fuse_file_info *file_info)
{
  S3_LOG(LOG_DEBUG, "readdir", "path: %s\n", path);
  ++s_readdir;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT_AS(directory, S_IFDIR, dir, path);

    return dir->read(bind(&dir_filler, filler, buf, _1));
  END_TRY;
}

int s3fuse_readlink(const char *path, char *buffer, size_t max_size)
{
  S3_LOG(LOG_DEBUG, "readlink", "path: %s, max_size: %zu\n", path, max_size);
  ++s_readlink;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT_AS(s3::fs::symlink, S_IFLNK, link, path);

    string target;
    int r = link->read(&target);

    if (r)
      return r;

    // leave room for the terminating null
    max_size--;

    if (target.size() < max_size)
      max_size = target.size();

    memcpy(buffer, target.c_str(), max_size);
    buffer[max_size] = '\0';

    return 0;
  END_TRY;
}

int s3fuse_release(const char *path, fuse_file_info *file_info)
{
  file *f = file::from_handle(file_info->fh);

  S3_LOG(LOG_DEBUG, "release", "path: %s\n", f->get_path().c_str());

  BEGIN_TRY;
    return f->release();
  END_TRY;
}

int s3fuse_removexattr(const char *path, const char *name)
{
  S3_LOG(LOG_DEBUG, "removexattr", "path: %s, name: %s\n", path, name);

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    int r = obj->remove_metadata(name);

    return r ? r : obj->commit();
  END_TRY;
}

int s3fuse_rename(const char *from, const char *to)
{
  S3_LOG(LOG_DEBUG, "rename", "from: %s, to: %s\n", from, to);
  ++s_rename;

  ASSERT_VALID_PATH(from);
  ASSERT_VALID_PATH(to);

  BEGIN_TRY;
    GET_OBJECT(from_obj, from);

    // not using GET_OBJECT() here because we don't want to fail if "to"
    // doesn't exist
    object::ptr to_obj = cache::get(to);

    directory::invalidate_parent(from);
    directory::invalidate_parent(to);

    if (to_obj) {
      int r;

      if (to_obj->get_type() == S_IFDIR) {
        if (from_obj->get_type() != S_IFDIR)
          return -EISDIR;

        if (!static_pointer_cast<directory>(to_obj)->is_empty())
          return -ENOTEMPTY;

      } else if (from_obj->get_type() == S_IFDIR) {
        return -ENOTDIR;
      }

      r = to_obj->remove();

      if (r)
        return r;
    }

    return from_obj->rename(to);
  END_TRY;
}

#ifdef __APPLE__
int s3fuse_setxattr(const char *path, const char *name, const char *value, size_t size, int flags, uint32_t position)
#else
int s3fuse_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
#endif
{
  S3_LOG(LOG_DEBUG, "setxattr", "path: [%s], name: [%s], size: %i\n", path, name, size);

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    bool needs_commit = false;
    GET_OBJECT(obj, path);

    int r = obj->set_metadata(name, value, size, flags, &needs_commit);

    if (r)
      return r;

    return needs_commit ? obj->commit() : 0;
  END_TRY;
}

int s3fuse_statfs(const char * /* ignored */, struct statvfs *s)
{
  s->f_namemax = 1024; // arbitrary

  s->f_bsize = object::get_block_size();

  s->f_blocks = numeric_limits<typeof(s->f_blocks)>::max();
  s->f_bfree = numeric_limits<typeof(s->f_bfree)>::max();
  s->f_bavail = numeric_limits<typeof(s->f_bavail)>::max();
  s->f_files = numeric_limits<typeof(s->f_files)>::max();
  s->f_ffree = numeric_limits<typeof(s->f_ffree)>::max();

  return 0;
}

int s3fuse_symlink(const char *target, const char *path)
{
  const fuse_context *ctx = fuse_get_context();

  S3_LOG(LOG_DEBUG, "symlink", "path: %s, target: %s\n", path, target);
  ++s_symlink;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    symlink::ptr link;

    if (cache::get(path)) {
      S3_LOG(LOG_WARNING, "symlink", "attempt to overwrite object at [%s]\n", path);
      return -EEXIST;
    }

    directory::invalidate_parent(path);

    link.reset(new s3::fs::symlink(path));

    link->set_uid(ctx->uid);
    link->set_gid(ctx->gid);

    link->set_target(target);

    return link->commit();
  END_TRY;
}

int s3fuse_unlink(const char *path)
{
  S3_LOG(LOG_DEBUG, "unlink", "path: %s\n", path);
  ++s_unlink;

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    directory::invalidate_parent(path);

    return obj->remove();
  END_TRY;
}

int s3fuse_utimens(const char *path, const timespec times[2])
{
  S3_LOG(LOG_DEBUG, "utimens", "path: %s, time: %li\n", path, times[1].tv_sec);

  ASSERT_VALID_PATH(path);

  BEGIN_TRY;
    GET_OBJECT(obj, path);

    obj->set_mtime(times[1].tv_sec);

    return obj->commit();
  END_TRY;
}

int s3fuse_write(const char *path, const char *buffer, size_t size, off_t offset, fuse_file_info *file_info)
{
  BEGIN_TRY;
    return file::from_handle(file_info->fh)->write(buffer, size, offset);
  END_TRY;
}

int print_version()
{
  printf(
    "%s, %s, version %s\n", 
    s3::base::APP_FULL_NAME, 
    s3::base::APP_NAME, 
    s3::base::APP_VERSION);

  return 0;
}

int print_usage(const char *arg0)
{
  fprintf(stderr, 
    "Usage: %s [options] <mountpoint>\n"
    "\n"
    "Options:\n"
    "  -f                   stay in the foreground (i.e., do not daemonize)\n"
    "  -h, --help           print this help message and exit\n"
    "  -o OPT...            pass OPT (comma-separated) to FUSE, such as:\n"
    "     allow_other         allow other users to access the mounted file system\n"
    "     allow_root          allow root to access the mounted file system\n"
    "     config=<file>       use <file> rather than the default configuration file\n"
    "     stats=<file>        write statistics to <file>\n"
    #ifdef __APPLE__
      "     daemon_timeout=<n>  set fuse timeout to <n> seconds\n"
      "     noappledouble       disable testing for/creating .DS_Store files on Mac OS\n"
    #endif
    "  -v, --verbose        enable logging to stderr (can be repeated for more verbosity)\n"
    "  -V, --version        print version and exit\n",
    arg0);

  return 0;
}

int process_argument(void *data, const char *arg, int key, struct fuse_args *out_args)
{
  if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
    print_version();
    exit(0);
  }

  if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
    print_usage(s_opts.arg0);
    exit(1);
  }

  if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
    s_opts.verbosity++;
    return 0;
  }

  if (strstr(arg, "config=") == arg) {
    s_opts.config = arg + 7;
    return 0;
  }

  if (strstr(arg, "stats=") == arg) {
    s_opts.stats = arg + 6;
    return 0;
  }

  #ifdef __APPLE__
    if (strstr(arg, "daemon_timeout=") == arg) {
      s_opts.daemon_timeout_set = true;
      return 1; // continue processing
    }

    if (strstr(arg, "noappledouble") == arg) {
      s_opts.noappledouble_set = true;
      return 1; // continue processing
    }
  #endif

  if (key == FUSE_OPT_KEY_NONOPT)
    s_opts.mountpoint = arg; // assume that the mountpoint is the only non-option

  return 1;
}

void * init(fuse_conn_info *info)
{
  if (info->capable & FUSE_CAP_ATOMIC_O_TRUNC) {
    info->want |= FUSE_CAP_ATOMIC_O_TRUNC;
    S3_LOG(LOG_DEBUG, "init", "enabling FUSE_CAP_ATOMIC_O_TRUNC\n");
  } else {
    S3_LOG(LOG_WARNING, "init", "FUSE_CAP_ATOMIC_O_TRUNC not supported, will revert to truncate-then-open\n");
  }

  // this has to be here, rather than in main(), because the threads init()
  // creates won't survive the fork in fuse_main().
  pool::init();

  return NULL;
}

void build_operations(fuse_operations *ops)
{
  memset(ops, 0, sizeof(*ops));

  // TODO: add truncate()

  ops->flag_nullpath_ok = 1;

  // not an actual FS operation
  ops->init = init;

  ops->chmod = s3fuse_chmod;
  ops->chown = s3fuse_chown;
  ops->create = s3fuse_create;
  ops->getattr = s3fuse_getattr;
  ops->getxattr = s3fuse_getxattr;
  ops->flush = s3fuse_flush;
  ops->ftruncate = s3fuse_ftruncate;
  ops->listxattr = s3fuse_listxattr;
  ops->mkdir = s3fuse_mkdir;
  ops->open = s3fuse_open;
  ops->read = s3fuse_read;
  ops->readdir = s3fuse_readdir;
  ops->readlink = s3fuse_readlink;
  ops->release = s3fuse_release;
  ops->removexattr = s3fuse_removexattr;
  ops->rename = s3fuse_rename;
  ops->rmdir = s3fuse_unlink;
  ops->setxattr = s3fuse_setxattr;
  ops->statfs = s3fuse_statfs;
  ops->symlink = s3fuse_symlink;
  ops->unlink = s3fuse_unlink;
  ops->utimens = s3fuse_utimens;
  ops->write = s3fuse_write;
}

void build_options(int argc, char **argv, fuse_args *args)
{
  struct stat mp_stat;

  s_opts.verbosity = LOG_WARNING;
  s_opts.arg0 = argv[0];

  #ifdef __APPLE__
    s_opts.noappledouble_set = false;
    s_opts.daemon_timeout_set = false;
  #endif

  fuse_opt_parse(args, NULL, NULL, process_argument);

  if (s_opts.mountpoint.empty()) {
    print_usage(s_opts.arg0);
    exit(1);
  }

  if (stat(s_opts.mountpoint.c_str(), &mp_stat)) {
    fprintf(stderr, "Failed to stat mount point.\n");
    exit(1);
  }

  s_opts.mountpoint_mode = S_IFDIR | mp_stat.st_mode;

  #ifdef __APPLE__
    // TODO: maybe make this and -o noappledouble the default?

    if (!s_opts.daemon_timeout_set)
      fprintf(stderr, "Set \"-o daemon_timeout=3600\" or something equally large if transferring large files, otherwise FUSE will time out.\n");

    if (!s_opts.noappledouble_set)
      fprintf(stderr, "You are *strongly* advised to pass \"-o noappledouble\" to disable the creation/checking/etc. of .DS_Store files.\n");
  #endif
}

#ifdef OSX_BUNDLE
  void set_osx_default_options(int *argc, char ***argv)
  {
    char *arg0 = *argv[0];
    string options, mountpoint, config;

    config = getenv("HOME");
    config += "/.s3fuse/s3fuse.conf";

    try {
      logger::init(LOG_ERR);
      config::init(config);
    } catch (...) {
      exit(1);
    }

    options = "noappledouble";
    options += ",daemon_timeout=3600";
    options += ",config=" + config;
    options += ",volname=s3fuse volume (" + config::get_bucket_name() + ")";

    mountpoint = "/Volumes/s3fuse_" + config::get_bucket_name();

    mkdir(mountpoint.c_str(), 0777);

    *argc = 4;
    *argv = new char *[5];

    (*argv)[0] = arg0;
    (*argv)[1] = strdup("-o");
    (*argv)[2] = strdup(options.c_str());
    (*argv)[3] = strdup(mountpoint.c_str());
    (*argv)[4] = NULL;
  }
#endif

int main(int argc, char **argv)
{
  int r;
  fuse_operations ops;

  #ifdef OSX_BUNDLE
    if (argc == 1)
      set_osx_default_options(&argc, &argv);
  #endif

  fuse_args args = FUSE_ARGS_INIT(argc, argv);
  build_options(argc, argv, &args);

  try {
    logger::init(s_opts.verbosity);
    config::init(s_opts.config);
    service::init(config::get_service());
    xml::init(service::get_xml_namespace());
    cache::init();
    encryption::init();

    file::test_transfer_chunk_sizes();

    if (!s_opts.stats.empty())
      statistics::init(s_opts.stats);

  } catch (const std::exception &e) {
    S3_LOG(LOG_ERR, "main", "caught exception while initializing: %s\n", e.what());
    return 1;
  }

  build_operations(&ops);

  S3_LOG(LOG_INFO, "main", "%s version %s, initialized\n", s3::base::APP_NAME, s3::base::APP_VERSION);

  r = fuse_main(args.argc, args.argv, &ops, NULL);

  fuse_opt_free_args(&args);

  pool::terminate();

  // these won't do anything if statistics::init() wasn't called
  statistics::collect();
  statistics::flush();

  return r;
}
