#include <boost/lexical_cast.hpp>
#include <boost/detail/atomic_count.hpp>

#include "base/config.h"
#include "base/logger.h"
#include "base/request.h"
#include "base/statistics.h"
#include "crypto/base64.h"
#include "crypto/encoder.h"
#include "crypto/hash.h"
#include "crypto/hex_with_quotes.h"
#include "crypto/md5.h"
#include "services/file_transfer.h"
#include "services/multipart_transfer.h"
#include "threads/pool.h"

using boost::lexical_cast;
using boost::scoped_ptr;
using boost::detail::atomic_count;
using std::ostream;
using std::string;
using std::vector;

using s3::base::char_vector;
using s3::base::char_vector_ptr;
using s3::base::config;
using s3::base::request;
using s3::base::statistics;
using s3::crypto::base64;
using s3::crypto::encoder;
using s3::crypto::hash;
using s3::crypto::hex_with_quotes;
using s3::crypto::md5;
using s3::services::file_transfer;
using s3::services::multipart_transfer;
using s3::threads::pool;

namespace
{
  struct download_range
  {
    size_t size;
    off_t offset;
  };

  typedef multipart_transfer<download_range> multipart_download;

  atomic_count s_downloads_single(0), s_downloads_single_failed(0);
  atomic_count s_downloads_multi(0), s_downloads_multi_failed(0), s_downloads_multi_chunks_failed(0);
  atomic_count s_uploads_single(0), s_uploads_single_failed(0);

  void statistics_writer(ostream *o)
  {
    *o <<
      "file_transfer single-part downloads:\n"
      "  succeeded: " << s_downloads_single << "\n"
      "  failed: " << s_downloads_single_failed << "\n"
      "file_transfer multi-part downloads:\n"
      "  succeeded: " << s_downloads_multi << "\n"
      "  failed: " << s_downloads_multi_failed << "\n"
      "  chunks failed: " << s_downloads_multi_chunks_failed << "\n"
      "file_transfer single-part uploads:\n"
      "  succeeded: " << s_uploads_single << "\n"
      "  failed: " << s_uploads_single_failed << "\n";
  }

  statistics::writers::entry s_writer(statistics_writer, 0);

  int download_part(const request::ptr &req, const string &url, download_range *range, const file_transfer::write_chunk_fn &on_write, bool is_retry)
  {
    long rc = 0;

    // yes, relying on is_retry will result in the chunks failed count being off by one, maybe, but we don't care

    if (is_retry)
      ++s_downloads_multi_chunks_failed; 

    req->init(s3::base::HTTP_GET);
    req->set_url(url);
    req->set_header("Range", 
      string("bytes=") + 
      lexical_cast<string>(range->offset) + 
      string("-") + 
      lexical_cast<string>(range->offset + range->size));

    req->run(config::get_transfer_timeout_in_s());
    rc = req->get_response_code();

    if (rc != s3::base::HTTP_SC_PARTIAL_CONTENT)
      return -EIO;
    else if (req->get_output_buffer().size() < range->size)
      return -EIO;

    return on_write(&req->get_output_buffer()[0], range->size, range->offset);
  }

  int download_multi(const string &url, size_t size, const file_transfer::write_chunk_fn &on_write)
  {
    scoped_ptr<multipart_download> dl;
    size_t num_parts = (size + config::get_download_chunk_size() - 1) / config::get_download_chunk_size();
    vector<download_range> parts(num_parts);

    for (size_t i = 0; i < num_parts; i++) {
      download_range *range = &parts[i];

      range->offset = i * config::get_download_chunk_size();
      range->size = (i != num_parts - 1) ? config::get_download_chunk_size() : (size - config::get_download_chunk_size() * i);
    }

    dl.reset(new multipart_download(
      parts,
      bind(&download_part, _1, url, _2, on_write, false),
      bind(&download_part, _1, url, _2, on_write, true)));

    return dl->process();
  }

  int increment_on_result(int r, atomic_count *success, atomic_count *failure)
  {
    if (r)
      ++(*failure);
    else
      ++(*success);

    return r;
  }
}

file_transfer::~file_transfer()
{
}

int file_transfer::download(const string &url, size_t size, const write_chunk_fn &on_write)
{
  if (size > config::get_download_chunk_size())
    return increment_on_result(
      download_multi(url, size, on_write), 
      &s_downloads_single, 
      &s_downloads_single_failed);
  else
    return increment_on_result(
      pool::call(threads::PR_REQ_1, bind(&file_transfer::download_single, _1, url, size, on_write)),
      &s_downloads_multi,
      &s_downloads_multi_failed);
}

int file_transfer::upload(const string &url, size_t size, const read_chunk_fn &on_read, string *returned_etag)
{
  return increment_on_result(
    pool::call(threads::PR_REQ_1, bind(&file_transfer::upload_single, _1, url, size, on_read, returned_etag)),
    &s_uploads_single,
    &s_uploads_single_failed);
}

int file_transfer::download_single(const request::ptr &req, const string &url, size_t size, const write_chunk_fn &on_write)
{
  long rc = 0;

  req->init(base::HTTP_GET);
  req->set_url(url);

  req->run(config::get_transfer_timeout_in_s());
  rc = req->get_response_code();

  if (rc == base::HTTP_SC_NOT_FOUND)
    return -ENOENT;
  else if (rc != base::HTTP_SC_OK)
    return -EIO;

  return on_write(&req->get_output_buffer()[0], req->get_output_buffer().size(), 0);
}

int file_transfer::upload_single(const request::ptr &req, const string &url, size_t size, const read_chunk_fn &on_read, string *returned_etag)
{
  int r = 0;
  char_vector_ptr buffer(new char_vector());
  string expected_md5_b64, expected_md5_hex, etag;
  uint8_t read_hash[md5::HASH_LEN];

  r = on_read(size, 0, buffer);

  if (r)
    return r;

  hash::compute<md5>(*buffer, read_hash);

  expected_md5_b64 = encoder::encode<base64>(read_hash, md5::HASH_LEN);
  expected_md5_hex = encoder::encode<hex_with_quotes>(read_hash, md5::HASH_LEN);

  req->init(s3::base::HTTP_PUT);
  req->set_url(url);

  req->set_header("Content-MD5", expected_md5_b64);
  req->set_input_buffer(buffer);

  req->run(config::get_transfer_timeout_in_s());

  if (req->get_response_code() != s3::base::HTTP_SC_OK) {
    S3_LOG(LOG_WARNING, "file_transfer::upload_single", "failed to upload for [%s].\n", url.c_str());
    return -EIO;
  }

  etag = req->get_response_header("ETag");

  if (md5::is_valid_quoted_hex_hash(etag) && etag != expected_md5_hex) {
    S3_LOG(LOG_WARNING, "file_transfer::upload_single", "etag [%s] does not match md5 [%s].\n", etag.c_str(), expected_md5_hex.c_str());
    return -EIO;
  }
  
  *returned_etag = etag;

  return 0;
}

size_t file_transfer::get_download_chunk_size()
{
  return config::get_download_chunk_size();
}

size_t file_transfer::get_upload_chunk_size()
{
  return 0; // this file_transfer impl doesn't do chunks
}