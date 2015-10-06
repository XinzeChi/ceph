/*
 * This is the tool to parse ceph serial number
 * serial number format looks like uuid:
 * 9191D01F-C26B-D4E2-CE76-B9700C35D2D2
 * command exmaples:
 *      ./xsky-decrypt-tool 9191D01F-C26B-D4E2-CE76-B9700C35D2D2
 *      {
 *	    "time": "9744043954",
 *	    "osds": "100"
 *	}
 */

#include "aes_crypt.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>

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
