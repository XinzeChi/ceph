/*
 * xsky license generate tool
 * serial number format looks like uuid:
 * 9191D01F-C26B-D4E2-CE76-B9700C35D2D2
 * command exmaples:
 * 	//generate serial number for 100 osds with interval 12 months
 * 	./xsky-encrypt-tool 100 $((12*30*24*3600))
 *
 */

#include "aes_crypt.h"

void usage()
{
  cout << "usage: <osds> <seconds>" << endl;
}

int main(int argc, char *argv[])
{
  if (argc < 3) {
    usage();
    exit(-1);
  }
  string plain_text, cipher_text;
  string str_key = "www.xsky.com";
  string str_iv = "www.xsky.com";
  init_kv(str_key, str_iv);

  struct sn_item sn(atoi(argv[1]), strtoul(argv[2], NULL, 10));
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

  return 0;
}
