/*
 * This is the tool to parse ceph serial number
 * serial number format looks like uuid:
 * 9191D01F-C26B-D4E2-CE76-B9700C35D2D2
 * command exmaples:
 *      ./ceph_sn 9191D01F-C26B-D4E2-CE76-B9700C35D2D2
 *      {
 *	    "time": "9744043954",
 *	    "osds": "100"
 *	}
 */

#include <iostream>
using std::cout;
using std::cerr;
using std::endl;
#include <iomanip>

#include <string>
using std::string;

#include <cstdlib>
using std::exit;

#include <cryptopp/cryptlib.h>
using CryptoPP::Exception;

#include <cryptopp/hex.h>
using CryptoPP::HexEncoder;
using CryptoPP::HexDecoder;

#include <cryptopp/filters.h>
using CryptoPP::StringSink;
using CryptoPP::StringSource;
using CryptoPP::StreamTransformationFilter;

#include <cryptopp/aes.h>
using CryptoPP::AES;

#include <cryptopp/ccm.h>
using CryptoPP::CBC_Mode;

#include <assert.h>
#include <sys/time.h>
#include <sstream>
#include <stdio.h>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

//AutoSeededRandomPool prng;
byte key[AES::DEFAULT_KEYLENGTH];
byte iv[AES::BLOCKSIZE];

void init_kv(string str_key, string str_iv)
{
  //prng.GenerateBlock(key, sizeof(key));
  //prng.GenerateBlock(iv, sizeof(iv));
  memset(key, 0x00, AES::DEFAULT_KEYLENGTH);
  memset(iv, 0x00, AES::BLOCKSIZE);
  unsigned i = 0;
  for (i = 0; i < AES::DEFAULT_KEYLENGTH && i < str_key.size(); i++) {
    key[i] = str_key[i];
  }
  for (i = 0; i < AES::BLOCKSIZE && i < str_iv.size(); i++) {
    iv[i] = str_iv[i];
  }

  string encoded;
  encoded.clear();
  StringSource(key, sizeof(key), true,
    new HexEncoder(
      new StringSink(encoded)
    ) // HexEncoder
  ); // StringSource

  encoded.clear();
  StringSource(iv, sizeof(iv), true,
  new HexEncoder(
    new StringSink(encoded)
    ) // HexEncoder
  ); // StringSource
}

string encrypt(string plain)
{
  string cipher, encoded, recovered;

  try
  {
    CBC_Mode< AES >::Encryption e;
    e.SetKeyWithIV(key, sizeof(key), iv);

    // The StreamTransformationFilter removes padding as required.
    StringSource s(plain, true,
      new StreamTransformationFilter(e,
        new StringSink(cipher)
      ) // StreamTransformationFilter
    ); // StringSource
  }
  catch(const CryptoPP::Exception& e)
  {
    cerr << __func__ << ": " << e.what() << endl;
    exit(1);
  }

  encoded.clear();
  StringSource(cipher, true,
    new HexEncoder(
     new StringSink(encoded)
    ) // HexEncoder
  ); // StringSource

  return encoded;
}

string decrypt(string cipher_hex)
{
  string decoded, recovered;

  decoded.clear();
  StringSource(cipher_hex, true,
    new HexDecoder(
      new StringSink(decoded)
    ) //HexDecoder
  ); //StringSource

  try
  {
    CBC_Mode< AES >::Decryption d;
    d.SetKeyWithIV(key, sizeof(key), iv);

    // The StreamTransformationFilter removes padding as required.
    StringSource s(decoded, true,
      new StreamTransformationFilter(d,
        new StringSink(recovered)
      ) // StreamTransformationFilter
    ); // StringSource

  }
  catch(const CryptoPP::Exception& e)
  {
    cerr << __func__ << ": " << e.what() << endl;
    exit(1);
  }

  return recovered;
}

struct sn_item {
  time_t t;
  int osds;
  sn_item(int num, time_t addition = 0): osds(num)
  {
    time(&t);
    t = t + addition;
  }
  string gen_sn_plain() {
    string str;
    std::stringstream ss;
    ss << std::setw(10) << t;
    ss << std::setfill('0') <<std::setw(5) << osds;
    ss >> str;
    return str;
  }
};

string gen_sn(struct sn_item *sn_item)
{
  string plain;
  string sn;
  string cipher;
  plain = sn_item->gen_sn_plain();
  //cout << "sn_plain: " << plain << endl;
  cipher = encrypt(plain);
  //cout << "cipher: " << cipher << endl;
  int lens[] = {8, 4, 4, 4, 12};
  int count = 5; //sizeof(lens)/sizeof(int);
  int i = 0;
  int pos = 0;
  sn = cipher.substr(0, lens[0]);
  for (i = 0; i < count - 1; i++) {
    pos += lens[i];
    sn += "-" + cipher.substr(pos, lens[i+1]);
  }
  return sn;
}

int parse_sn(string &sn, time_t &time, int &osds)
{
  time = 0;
  osds = 0;
  if (sn.length() != 36) {
    cout << "serial number length must be 36" << endl;
    return -1;
  }
  string plain;
  string cipher;
  int lens[] = {8, 4, 4, 4, 12};
  int count = 5; //sizeof(lens)/sizeof(int);
  int i = 0;
  int pos = 0;
  cipher = sn.substr(0,lens[0]);
  for (i = 0; i < count - 1; i++) {
    pos += 1 + lens[i]; //with "-"
    if (sn.substr(pos - 1, 1) != "-")
      return -1;
    cipher += sn.substr(pos, lens[i+1]);
  }
  plain = decrypt(cipher);
  time = strtoul(plain.substr(0, 10).c_str(), NULL, 10);
  osds = atoi(plain.substr(10,5).c_str());
  return 0;
}

void usage()
{
  cout << "usage: <SN>" << endl;
}

int main(int argc, char *argv[])
{
  if (argc < 2) {
    usage();
    exit(-1);
  }
  string text = argv[1];
  string plain_text, cipher_text;
  string str_key = "www.xsky.com";
  string str_iv = "www.xsky.com";
  init_kv(str_key, str_iv);

  time_t t;
  int osds;
  int r;
  r = parse_sn(text, t, osds);
  if (r)
    return r;
  boost::property_tree::ptree pt;
  std::stringstream ss;
  pt.put("time", t);
  pt.put("osds", osds);
  boost::property_tree::write_json(ss, pt);
  cout << ss.str();

  return 0;
}
