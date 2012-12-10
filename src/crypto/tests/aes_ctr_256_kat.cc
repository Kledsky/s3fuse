#include <string.h>

#include <iostream>
#include <stdexcept>

#include "crypto/aes_ctr_256.h"
#include "crypto/encoder.h"
#include "crypto/hex.h"
#include "crypto/symmetric_key.h"

using std::cin;
using std::cout;
using std::endl;
using std::runtime_error;
using std::string;
using std::vector;

using s3::crypto::aes_ctr_256;
using s3::crypto::encoder;
using s3::crypto::hex;
using s3::crypto::symmetric_key;

int main(int argc, char **argv)
{
  string key, iv, starting_block, plaintext, ciphertext;

  while (!cin.eof()) {
    string line, first, last;
    size_t pos;

    getline(cin, line);
    pos = line.find(": ");

    if (line[0] == '#' || pos == string::npos)
      continue;

    first = line.substr(0, pos);
    last = line.substr(pos + 2);

    if (first == "key")
      key = last;
    else if (first == "iv")
      iv = last;
    else if (first == "starting_block")
      starting_block = last;
    else if (first == "plaintext")
      plaintext = last;
    else if (first == "ciphertext")
      ciphertext = last;

    if (!key.empty() && !iv.empty() && !starting_block.empty() && !plaintext.empty() && !ciphertext.empty()) {
      symmetric_key::ptr cs;
      vector<uint8_t> in_buf, out_buf;
      string in_enc, out_enc;
      uint64_t sb = 0;

      try {
        cs = symmetric_key::from_string(key + ":" + iv);

        encoder::decode<hex>(starting_block, &in_buf);
        out_buf.resize(in_buf.size());

        if (in_buf.size() != sizeof(sb))
          throw runtime_error("starting block not of correct length");

        // reverse endianness of starting_block
        for (size_t i = 0; i < in_buf.size(); i++)
          out_buf[in_buf.size() - i - 1] = in_buf[i];

        memcpy(&sb, &out_buf[0], sizeof(sb));

        encoder::decode<hex>(plaintext, &in_buf);
        out_buf.resize(in_buf.size());

        aes_ctr_256::encrypt_with_starting_block(cs, sb, &in_buf[0], in_buf.size(), &out_buf[0]);
        out_enc = encoder::encode<hex>(out_buf);

        if (out_enc != ciphertext)
          throw runtime_error("ciphertext does not match");

        aes_ctr_256::decrypt_with_starting_block(cs, sb, &out_buf[0], out_buf.size(), &in_buf[0]);
        in_enc = encoder::encode<hex>(in_buf);

        if (in_enc != plaintext)
          throw runtime_error("failed decryption");

        cout << "PASSED: key len: " << cs->get_key()->size() * 8 << " bits, plain text len: " << in_buf.size() << " bytes" << endl;

      } catch (const std::exception &e) {
        cout 
          << "FAILED: " << e.what() << endl
          << "  key: " << key << endl
          << "  iv: " << iv << endl
          << "  starting block: " << starting_block << endl
          << "  plain text: " << plaintext << endl
          << "  cipher text: " << ciphertext << endl
          << "  state: " << (cs ? cs->to_string() : string("n/a")) << endl
          << "  aes out: " << out_enc << endl;

        return 1;
      }

      key.clear();
      iv.clear();
      starting_block.clear();
      plaintext.clear();
      ciphertext.clear();
    }
  }

  return 0;
}