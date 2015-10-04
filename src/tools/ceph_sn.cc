/*
 * This is the tool to generate ceph serial number
 * serial number format looks like uuid:
 * CC60BBA9-DBF2-F1A7-6AE7-E9523653515C
 * command exmaples:
 *      ./ceph_sn encrypt 100 $((12*30*24*3600))  //generate serial number for 100 osds with interval 12 months
 *      ./ceph_sn decrypt CC60BBA9-DBF2-F1A7-6AE7-E9523653515C // decode SN
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

void parse_sn(string &sn, time_t &time, int &osds)
{
  time = 0;
  osds = 0;
  if (sn.length() != 36) {
    cout << "serial number length must be 36" << endl;
    return;
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
      return;
    cipher += sn.substr(pos, lens[i+1]);
  }
  plain = decrypt(cipher);
  time = strtoul(plain.substr(0, 10).c_str(), NULL, 10);
  osds = atoi(plain.substr(10,5).c_str());
}

void usage()
{
  cout << "usage: encrypt <osds> <seconds>" << endl
        <<  "       decrypt <SN>"
        << endl;
}

int main(int argc, char *argv[])
{
  if (argc < 3) {
    usage();
    exit(-1);
  }
  string type = argv[1];
  string text = argv[2];
  string plain_text, cipher_text;
  string str_key = "www.xsky.com";
  string str_iv = "www.xsky.com";
  init_kv(str_key, str_iv);

  if (type == "encrypt") {
    if ( argc < 4 ) {
      usage();
      exit(-1);
    }

    struct sn_item sn(atoi(argv[2]), strtoul(argv[3], NULL, 10));
    const time_t unixtime_max = 9999999999; // 10 chars
    const int max_osds = 99999; // 5 chars
    if (sn.t > unixtime_max) {
      cout << "time: "<< sn.t << " larger than unixtime_max: " << unixtime_max << endl;
      exit(-1);
    }
    if (sn.osds > max_osds) {
      cout << "osds: " << sn.osds << " larger than max_osds: " << max_osds << endl;
      exit(-1);
    }
    string plain;
    plain = sn.gen_sn_plain();
    cipher_text = gen_sn(&sn);
    cout << cipher_text << endl;
  } else if(type == "decrypt") {
    time_t t;
    int osds;
    parse_sn(text, t, osds);
    cout << "time: " << t << endl
         << "osds: " << osds << endl;
  }

  return 0;
}
