#include "privileges.h"
#include <errno.h>
#ifdef __linux__
#include <linux/capability.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif
int ra_privileges_restrict_exec(void) {
#ifdef __linux__
   /* Prevent file capabilities or setuid exec from reacquiring privilege. */
   if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) return -1;
#ifdef PR_CAP_AMBIENT
   if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) < 0 &&
       errno != EINVAL) return -1;
#endif
#if defined(SYS_capset) && defined(_LINUX_CAPABILITY_VERSION_3)
   struct __user_cap_header_struct header = {
      .version = _LINUX_CAPABILITY_VERSION_3, .pid = 0
   };
   struct __user_cap_data_struct caps[2] = {{0}, {0}};
   if (syscall(SYS_capset, &header, caps) < 0) return -1;
#endif
   return 0;
#else
   errno = ENOTSUP;
   return -1;
#endif
}
