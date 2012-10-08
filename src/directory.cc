#include "config.h"
#include "directory.h"
#include "logger.h"
#include "object_cache.h"
#include "request.h"
#include "service.h"
#include "thread_pool.h"
#include "util.h"
#include "xml.h"

using namespace boost;
using namespace std;

using namespace s3;

namespace
{
  const char *IS_TRUNCATED_XPATH = "/s3:ListBucketResult/s3:IsTruncated";
  const char *         KEY_XPATH = "/s3:ListBucketResult/s3:Contents/s3:Key";
  const char * NEXT_MARKER_XPATH = "/s3:ListBucketResult/s3:NextMarker";
  const char *      PREFIX_XPATH = "/s3:ListBucketResult/s3:CommonPrefixes/s3:Prefix";

  struct rename_operation
  {
    shared_ptr<string> old_name;
    wait_async_handle::ptr handle;
  };

  object * checker(const string &path, const request::ptr &req)
  {
    const string &url = req->get_url();

    S3_LOG(LOG_DEBUG, "directory::checker", "testing [%s]\n", path.c_str());

    if (url.empty() || url[url.size() - 1] != '/')
      return NULL;

    return new directory(path);
  }

  object::type_checker::type_checker s_checker_reg(checker, 10);

  int check_if_truncated(const xml::document &doc, bool *truncated)
  {
    int r;
    string temp;

    r = xml::find(doc, IS_TRUNCATED_XPATH, &temp);

    if (r)
      return r;

    *truncated = (temp == "true");
    return 0;
  }
}

string directory::build_url(const string &path)
{
  return service::get_bucket_url() + "/" + util::url_encode(path) + "/";
}

directory::directory(const string &path)
  : object(path)
{
  set_url(build_url(path));
  set_object_type(S_IFDIR);
}

directory::~directory()
{
}

int directory::read(const request::ptr &req, const filler_function &filler)
{
  string marker = "";
  string path = get_path();
  size_t path_len;
  bool truncated = true;
  cache_list_ptr cache;

  if (!path.empty())
    path += "/";

  path_len = path.size();

  if (config::get_cache_directories())
    cache.reset(new cache_list());

  req->init(HTTP_GET);

  while (truncated) {
    int r;
    xml::document doc;
    xml::element_list prefixes, keys;

    req->set_url(service::get_bucket_url(), string("delimiter=/&prefix=") + util::url_encode(path) + "&marker=" + marker);
    req->run();

    if (req->get_response_code() != HTTP_SC_OK)
      return -EIO;

    doc = xml::parse(req->get_output_buffer());

    if (!doc) {
      S3_LOG(LOG_WARNING, "directory::read", "failed to parse response.\n");
      return -EIO;
    }

    if ((r = check_if_truncated(doc, &truncated)))
      return r;

    if (truncated && (r = xml::find(doc, NEXT_MARKER_XPATH, &marker)))
      return r;

    if ((r = xml::find(doc, PREFIX_XPATH, &prefixes)))
      return r;

    if ((r = xml::find(doc, KEY_XPATH, &keys)))
      return r;

    for (xml::element_list::const_iterator itor = prefixes.begin(); itor != prefixes.end(); ++itor) {
      //const char *full_path_cs = itor->c_str();
      //const char *relative_path_cs = full_path_cs + path_len;
      string /*full_path,*/ relative_path;
      const char *relative_path_cs = itor->c_str() + path_len;

      // strip trailing slash
      // full_path.assign(full_path_cs, strlen(full_path_cs) - 1);
      relative_path.assign(relative_path_cs, strlen(relative_path_cs) - 1);

      // TODO: restore?
      /*
      if (config::get_precache_on_readdir())
        _tp_bg->call_async(bind(&fs::__prefill_stats, this, _1, full_path, HINT_IS_DIR));
      */

      filler(relative_path);

      if (cache)
        cache->push_back(relative_path);
    }

    for (xml::element_list::const_iterator itor = keys.begin(); itor != keys.end(); ++itor) {
      const char *full_path_cs = itor->c_str();

      if (strcmp(path.c_str(), full_path_cs) != 0) {
        string relative_path(full_path_cs + path_len);
        // string full_path(full_path_cs);

        // TODO: restore?
        /*
        if (config::get_precache_on_readdir())
          _tp_bg->call_async(bind(&fs::__prefill_stats, this, _1, full_path, HINT_IS_FILE));
        */

        filler(relative_path);

        if (cache)
          cache->push_back(relative_path);
      }
    }
  }

  if (cache) {
    mutex::scoped_lock lock(_mutex);

    _cache = cache;
  }

  return 0;
}

// TODO: find some way to make sure that is_empty() fails for the root directory (path == "")
bool directory::is_empty(const request::ptr &req)
{
  xml::document doc;
  xml::element_list keys;

  req->init(HTTP_GET);

  // set max-keys to two because GET will always return the path we request
  // note the trailing slash on path
  req->set_url(service::get_bucket_url(), string("prefix=") + util::url_encode(get_path()) + "/&max-keys=2");
  req->run();

  // if the request fails, assume the directory's not empty
  if (req->get_response_code() != HTTP_SC_OK)
    return false;

  doc = xml::parse(req->get_output_buffer());

  if (!doc) {
    S3_LOG(LOG_WARNING, "directory::is_empty", "failed to parse response.\n");
    return false;
  }

  if (xml::find(doc, KEY_XPATH, &keys))
    return false;

  return (keys.size() == 1);
}

int directory::remove(const request::ptr &req)
{
  if (!is_empty(req))
    return -ENOTEMPTY;

  return object::remove(req);
}

void directory::invalidate_parent(const string &path)
{
  if (config::get_cache_directories()) {
    string parent_path;
    size_t last_slash = path.rfind('/');

    parent_path = (last_slash == string::npos) ? "" : path.substr(0, last_slash);

    S3_LOG(LOG_DEBUG, "directory::invalidate_parent", "invalidating parent directory [%s] for [%s].\n", parent_path.c_str(), path.c_str());
    object_cache::remove(parent_path);
  }
}

int directory::rename(const request::ptr &req, const string &to_)
{
  string from, to;
  size_t from_len;
  string marker = "";
  bool truncated = true;
  list<rename_operation> pending_renames, pending_deletes;

  // can't do anything with the root directory
  if (get_path().empty())
    return -EINVAL;

  from = get_path() + "/";
  to = to_ + "/";
  from_len = from.size();

  req->init(HTTP_GET);

  while (truncated) {
    xml::document doc;
    xml::element_list keys;
    int r;

    req->set_url(service::get_bucket_url(), string("prefix=") + util::url_encode(from) + "&marker=" + marker);
    req->run();

    if (req->get_response_code() != HTTP_SC_OK)
      return -EIO;

    doc = xml::parse(req->get_output_buffer());

    if (!doc) {
      S3_LOG(LOG_WARNING, "directory::rename", "failed to parse response.\n");
      return -EIO;
    }

    if ((r = check_if_truncated(doc, &truncated)))
      return r;

    if (truncated && (r = xml::find(doc, NEXT_MARKER_XPATH, &marker)))
      return r;

    if ((r = xml::find(doc, KEY_XPATH, &keys)))
      return r;

    for (xml::element_list::const_iterator itor = keys.begin(); itor != keys.end(); ++itor) {
      rename_operation oper;
      const char *full_path_cs = itor->c_str();
      const char *relative_path_cs = full_path_cs + from_len;
      string new_name = to + relative_path_cs;

      object_cache::remove(*oper.old_name);

      oper.old_name.reset(new string(full_path_cs));
      oper.handle = thread_pool::post(thread_pool::PR_BG, bind(&object::copy_by_path, _1, *oper.old_name, new_name));

      pending_renames.push_back(oper);

      S3_LOG(LOG_DEBUG, "directory::rename", "[%s] -> [%s]\n", full_path_cs, new_name.c_str());
    }
  }

  while (!pending_renames.empty()) {
    int r;
    rename_operation &oper = pending_renames.front();

    r = oper.handle->wait();

    if (r)
      return r;

    oper.handle.reset();
    pending_deletes.push_back(oper);
    pending_renames.pop_front();
  }

  for (list<rename_operation>::iterator itor = pending_deletes.begin(); itor != pending_deletes.end(); ++itor)
    itor->handle = thread_pool::post(thread_pool::PR_BG, bind(&object::remove_by_url, _1, object::build_url(*itor->old_name)));

  while (!pending_deletes.empty()) {
    int r;
    const rename_operation &oper = pending_deletes.front();

    r = oper.handle->wait();
    pending_deletes.pop_front();

    if (r)
      return r;
  }

  return 0;
}
