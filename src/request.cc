#include "logging.hh"

#include <stdint.h>
#include <string.h>
#include <time.h>
#include <pugixml/pugixml.hpp>
#include <sys/stat.h>

#include <iostream>
#include <stdexcept>
#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>

#include "object.hh"
#include "openssl_locks.hh"
#include "request.hh"
#include "util.hh"

using namespace boost;
using namespace pugi;
using namespace std;

using namespace s3;

namespace
{
  size_t null_readdata(void *, size_t, size_t, void *)
  {
    return 0;
  }

  const string AMZ_HEADER_PREFIX = "x-amz-";

  // TODO: obviously, these should be config options
  string g_url_prefix = "https://s3.amazonaws.com";
  string g_aws_key = "AKIAJZHNXBKNRCUMV4IQ";
  string g_aws_secret = "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";
  bool g_verbose_requests = false;
}

request::request()
  : _total_run_time(0.0),
    _run_count(0)
{
  _curl = curl_easy_init();

  if (!_curl)
    throw runtime_error("curl_easy_init() failed.");

  openssl_locks::init();

  // stuff that's set in the ctor shouldn't be modified elsewhere, since the call to init() won't reset it

  // TODO: check for errors
  curl_easy_setopt(_curl, CURLOPT_VERBOSE, g_verbose_requests);
  curl_easy_setopt(_curl, CURLOPT_NOPROGRESS, true);
  curl_easy_setopt(_curl, CURLOPT_FOLLOWLOCATION, true);
  curl_easy_setopt(_curl, CURLOPT_ERRORBUFFER, _curl_error);
  curl_easy_setopt(_curl, CURLOPT_FILETIME, true);
  curl_easy_setopt(_curl, CURLOPT_HEADERFUNCTION, &request::process_header);
  curl_easy_setopt(_curl, CURLOPT_HEADERDATA, this);
  curl_easy_setopt(_curl, CURLOPT_WRITEFUNCTION, &request::process_output);
  curl_easy_setopt(_curl, CURLOPT_WRITEDATA, this);
  curl_easy_setopt(_curl, CURLOPT_NOSIGNAL, true);
}

request::~request()
{
  curl_easy_cleanup(_curl);

  S3_DEBUG(
    "request::~request", 
    "served %" PRIu64 " requests at an average of %.02f ms per request.\n", 
    _run_count,
    (_run_count && _total_run_time > 0.0) ? (_total_run_time / double(_run_count) * 1000.0) : 0.0);

  openssl_locks::release();
}

void request::init(http_method method)
{
  _curl_error[0] = '\0';
  _url.clear();
  _request_data.clear();
  _request_data_pos = 0;
  _response_data.clear();
  _response_headers.clear();
  _response_code = 0;
  _last_modified = 0;
  _headers.clear();
  _target_object.reset();

  set_input_file(NULL, 0);
  set_output_file(NULL);

  curl_easy_setopt(_curl, CURLOPT_CUSTOMREQUEST, NULL);
  curl_easy_setopt(_curl, CURLOPT_UPLOAD, false);
  curl_easy_setopt(_curl, CURLOPT_NOBODY, false);

  if (method == HTTP_DELETE) {
    _method = "DELETE";
    curl_easy_setopt(_curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(_curl, CURLOPT_NOBODY, true);

  } else if (method == HTTP_GET) {
    _method = "GET";

  } else if (method == HTTP_HEAD) {
    _method = "HEAD";
    curl_easy_setopt(_curl, CURLOPT_NOBODY, true);

  } else if (method == HTTP_PUT) {
    _method = "PUT";
    curl_easy_setopt(_curl, CURLOPT_UPLOAD, true);

  } else
    throw runtime_error("unsupported HTTP method.");
}

size_t request::read_request_data(char *data, size_t size, size_t items, void *context)
{
  request *req = static_cast<request *>(context);
  size_t full_size = size * items;
  size_t remaining = req->_request_data.size() - req->_request_data_pos;

  if (full_size > remaining)
    full_size = remaining;

  if (remaining > 0)
    req->_request_data.copy(data, full_size, req->_request_data_pos);

  req->_request_data_pos += full_size;
  return full_size;
}

size_t request::process_header(char *data, size_t size, size_t items, void *context)
{
  request *req = static_cast<request *>(context);
  char *pos;

  size *= items;

  if (data[size] != '\0')
    return size; // we choose not to handle the case where data isn't null-terminated

  pos = strchr(data, '\n');

  if (pos)
    *pos = '\0';

  // for some reason the ETag header (among others?) contains a carriage return
  pos = strchr(data, '\r'); 

  if (pos)
    *pos = '\0';

  pos = strchr(data, ':');

  if (!pos)
    return size; // no colon means it's not a header we care about

  *pos++ = '\0';

  if (*pos == ' ')
    pos++;

  if (req->_target_object)
    req->_target_object->request_process_header(data, pos);
  else
    req->_response_headers[data] = pos;

  return size;
}

size_t request::process_output(char *data, size_t size, size_t items, void *context)
{
  request *req = static_cast<request *>(context);
  ssize_t rc;

  // why even bother with "items"?
  size *= items;

  if (req->_output_file) {
    rc = pwrite(fileno(req->_output_file), data, size, req->_output_offset);

    if (rc == -1)
      return 0;

    req->_output_offset += rc;
    return rc;

  } else {
    req->_response_data.append(data, size);

    return size;
  }
}

void request::set_url(const string &url, const string &query_string)
{
  string curl_url = g_url_prefix + url;

  if (!query_string.empty()) {
    curl_url += (curl_url.find('?') == string::npos) ? "?" : "&";
    curl_url += query_string;
  }

  _url = url;
  curl_easy_setopt(_curl, CURLOPT_URL, curl_url.c_str());
}

void request::set_input_data(const std::string &s)
{
  _request_data = s;
  _request_data_pos = 0;

  curl_easy_setopt(_curl, CURLOPT_READFUNCTION, read_request_data);
  curl_easy_setopt(_curl, CURLOPT_READDATA, this);
  curl_easy_setopt(_curl, CURLOPT_INFILESIZE, s.size());
}

void request::set_input_file(FILE *f, size_t size)
{
  if (f) {
    curl_easy_setopt(_curl, CURLOPT_READFUNCTION, NULL);
    curl_easy_setopt(_curl, CURLOPT_READDATA, f);
    curl_easy_setopt(_curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(size));
  } else {
    curl_easy_setopt(_curl, CURLOPT_READFUNCTION, null_readdata);
    curl_easy_setopt(_curl, CURLOPT_READDATA, NULL);
    curl_easy_setopt(_curl, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>(0));
  }
}

void request::build_request_time()
{
  time_t sys_time;
  tm gm_time;
  char time_str[128];

  time(&sys_time);
  gmtime_r(&sys_time, &gm_time);
  strftime(time_str, 128, "%a, %d %b %Y %H:%M:%S GMT", &gm_time);

  _headers["Date"] = time_str;
}

void request::build_signature()
{
  string sig = "AWS " + g_aws_key + ":";
  string to_sign = _method + "\n" + _headers["Content-MD5"] + "\n" + _headers["Content-Type"] + "\n" + _headers["Date"] + "\n";

  for (header_map::const_iterator itor = _headers.begin(); itor != _headers.end(); ++itor)
    if (!itor->second.empty() && itor->first.substr(0, AMZ_HEADER_PREFIX.size()) == AMZ_HEADER_PREFIX)
      to_sign += itor->first + ":" + itor->second + "\n";

  to_sign += _url;
  _headers["Authorization"] = string("AWS ") + g_aws_key + ":" + util::sign(g_aws_secret, to_sign);
}

void request::set_meta_headers(const object::ptr &object)
{
  object->request_set_meta_headers(this); 
}

void request::run()
{
  curl_slist *headers = NULL;
  double elapsed_time = util::get_current_time();

  // sanity
  if (_url.empty())
    throw runtime_error("call set_url() first!");

  if (_method.empty())
    throw runtime_error("call set_method() first!");

  _response_data.clear();
  _response_headers.clear();

  build_request_time();
  build_signature();

  for (header_map::const_iterator itor = _headers.begin(); itor != _headers.end(); ++itor)
    if (!itor->second.empty())
      headers = curl_slist_append(headers, (itor->first + ": " + itor->second).c_str());

  curl_easy_setopt(_curl, CURLOPT_HTTPHEADER, headers);

  if (_target_object)
    _target_object->request_init();

  if (curl_easy_perform(_curl) != 0)
    throw runtime_error(_curl_error);

  curl_easy_getinfo(_curl, CURLINFO_RESPONSE_CODE, &_response_code);
  curl_easy_getinfo(_curl, CURLINFO_FILETIME, &_last_modified);

  // TODO: add loop for timeouts and whatnot
  elapsed_time = util::get_current_time() - elapsed_time;

  S3_DEBUG("request::run", "request for [%s] returned %li and took %.2f ms.\n", _url.c_str(), _response_code, elapsed_time * 1.0e3);

  // don't save the time for the first request since it's likely to be disproportionately large
  if (_run_count > 0)
    _total_run_time += elapsed_time;

  _run_count++;

  if (_target_object)
    _target_object->request_process_response(this);
}

