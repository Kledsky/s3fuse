#include <fcntl.h>
#include <sys/stat.h>

#include <iostream>
#include <boost/bind.hpp>
#include <boost/thread.hpp>

#include "crypto/aes_ctr_256_cipher.h"
#include "crypto/cipher_state.h"

using boost::bind;
using boost::thread_group;
using std::cout;
using std::endl;
using std::runtime_error;
using std::string;

using s3::crypto::aes_ctr_256_cipher;
using s3::crypto::cipher_state;

namespace
{
  const size_t THREADS = 8;
  const size_t CHUNK_SIZE = 8 * 1024;
}

void run_aes_thread(const cipher_state::ptr &cs, int fd_in, int fd_out, off_t offset, size_t size)
{
  aes_ctr_256_cipher::ptr aes(new aes_ctr_256_cipher(cs, offset / aes_ctr_256_cipher::BLOCK_LEN));

  while (size) {
    uint8_t buf_in[CHUNK_SIZE], buf_out[CHUNK_SIZE];
    ssize_t sz;

    sz = pread(fd_in, buf_in, (size > CHUNK_SIZE) ? CHUNK_SIZE : size, offset);

    if (sz == -1)
      throw runtime_error("pread() failed");

    if (sz == 0)
      break;

    aes->encrypt(buf_in, sz, buf_out);

    sz = pwrite(fd_out, buf_out, sz, offset);

    if (sz <= 0)
      throw runtime_error("pwrite() failed");

    offset += sz;
    size -= sz;
  }
}

int run_aes(const cipher_state::ptr &cs, const string &file_in, const string &file_out)
{
  int fd_in, fd_out;
  struct stat st;

  cout << "using " << cs->serialize() << endl;

  fd_in = open(file_in.c_str(), O_RDONLY);
  fd_out = open(file_out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
  
  if (fd_in == -1 || fd_out == -1)
    throw runtime_error("open() failed");

  fstat(fd_in, &st);

  if (st.st_size < THREADS * CHUNK_SIZE) {
    run_aes_thread(cs, fd_in, fd_out, 0, st.st_size);
  } else {
    thread_group threads;
    size_t num_chunks = (st.st_size + CHUNK_SIZE - 1) / CHUNK_SIZE;
    size_t bytes_per_thread = num_chunks / THREADS * CHUNK_SIZE;
    size_t bytes_last_thread = bytes_per_thread + num_chunks % THREADS * CHUNK_SIZE;

    for (size_t i = 0; i < THREADS - 1; i++) {
      cout << "starting " << bytes_per_thread << " bytes on thread " << i << endl;
      threads.create_thread(bind(&run_aes_thread, cs, fd_in, fd_out, i * bytes_per_thread, bytes_per_thread));
    }

    cout << "starting " << bytes_last_thread << " bytes on thread " << THREADS - 1 << endl;
    threads.create_thread(bind(&run_aes_thread, cs, fd_in, fd_out, (THREADS - 1) * bytes_per_thread, bytes_last_thread));

    threads.join_all();
  }

  close(fd_in);
  close(fd_out);

  cout << "done" << endl;
  return 0;
}

int main(int argc, char **argv)
{
  try {
    if (argc == 3)
      return run_aes(cipher_state::generate<aes_ctr_256_cipher>(), argv[1], argv[2]);
    else if (argc == 4)
      return run_aes(cipher_state::deserialize(argv[1]), argv[2], argv[3]);

  } catch (const std::exception &e) {
    cout << "caught exception: " << e.what() << endl;
    return 1;
  }

  cout
    << "usage:" << endl
    << "  " << argv[0] << " <file-in> <file-out>        encrypt <file-in> and write to <file-out>" << endl
    << "  " << argv[0] << " <key> <file-in> <file-out>  decrypt <file-in> using <key> and write to <file-out>" << endl;

  return 1;
}