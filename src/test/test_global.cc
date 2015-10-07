#include <vector>
#include <string>
#include <unistd.h>
#include "gtest/gtest.h"
#include <fcntl.h>

#include "common/config.h"
#include "include/int_types.h"
#include "include/msgr.h"
#include "global/global_context.h"
#include "global/global_init.h"

using namespace std;

TEST(LocalConf, TestOSDLocalConf)
{
  vector<string> standard_conf, local_conf;
  standard_conf.push_back("[global]\n");
  standard_conf.push_back("osd_data = .\n");
  local_conf.push_back("[global]\n");
  local_conf.push_back("filestore_max_alloc_hint_size=1024\n");
  int r = 0;
  int fd;
  {
    fd = ::open("ceph.conf", O_WRONLY|O_CREAT|O_TRUNC, 0755);
    ASSERT_GT(fd, 0);
    for (vector<string>::const_iterator it = standard_conf.begin();
        it != standard_conf.end(); ++it) {
      r = ::write(fd, it->c_str(), it->size());
      ASSERT_GT(r, 0);
    }
  }
  {
    fd = ::open("conf", O_WRONLY|O_CREAT|O_TRUNC, 0755);
    ASSERT_GT(fd, 0);
    for (vector<string>::const_iterator it = local_conf.begin();
        it != local_conf.end(); ++it) {
      r = ::write(fd, it->c_str(), it->size());
      ASSERT_GT(r, 0);
    }
  }
  vector <const char*> args;
  const char *k = "-c";
  const char *v = "ceph.conf";
  args.push_back(k);
  args.push_back(v);
  global_pre_init(NULL, args, CEPH_ENTITY_TYPE_OSD, CODE_ENVIRONMENT_DAEMON, 0);
  ASSERT_EQ(g_conf->filestore_max_alloc_hint_size, 1024);
}
