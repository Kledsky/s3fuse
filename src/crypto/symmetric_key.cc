#ifdef __APPLE__
  #include <Security/SecRandom.h>
#else
  #include <openssl/rand.h>
#endif

#include <stdexcept>

#include "crypto/encoder.h"
#include "crypto/hex.h"
#include "crypto/symmetric_key.h"

using std::runtime_error;
using std::string;

using s3::crypto::encoder;
using s3::crypto::hex;
using s3::crypto::symmetric_key;

symmetric_key::ptr symmetric_key::deserialize(const string &state)
{
  ptr cs(new symmetric_key());
  size_t pos;

  pos = state.find(':');

  if (pos == string::npos)
    throw runtime_error("malformed symmetric key string");

  encoder::decode<hex>(state.substr(0, pos), &cs->_key);
  encoder::decode<hex>(state.substr(pos + 1), &cs->_iv);

  return cs;
}

string symmetric_key::serialize()
{
  return 
    encoder::encode<hex>(&_key[0], _key.size()) + ":" +
    encoder::encode<hex>(&_iv[0], _iv.size());
}

void symmetric_key::generate(size_t key_len, size_t iv_len)
{
  _key.resize(key_len);
  _iv.resize(iv_len);

  // TODO: explicitly fail on big-endian machines!

  #ifdef __APPLE__
    if (SecRandomCopyBytes(kSecRandomDefault, _key.size(), &_key[0]) != 0)
      throw runtime_error("failed to generate random key");

    if (SecRandomCopyBytes(kSecRandomDefault, _iv.size(), &_iv[0]) != 0)
      throw runtime_error("failed to generate random IV");
  #else
    if (RAND_bytes(&_key[0], _key.size()) == 0)
      throw runtime_error("failed to generate random key");

    if (RAND_bytes(&_iv[0], _iv.size()) == 0)
      throw runtime_error("failed to generate random IV");
  #endif
}
