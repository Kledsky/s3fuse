#ifndef S3_CRYPTO_BASE64_H
#define S3_CRYPTO_BASE64_H

#include <stdint.h>

#include <string>
#include <vector>

namespace s3
{
  namespace crypto
  {
    class encoder;

    class base64
    {
    private:
      friend class encoder;

      static std::string encode(const uint8_t *input, size_t size);
      static void decode(const std::string &input, std::vector<uint8_t> *output);
    };
  }
}

#endif