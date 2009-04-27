#ifndef _FCGIEV_PREFIX_H_
#define _FCGIEV_PREFIX_H_

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <assert.h>
#include <string.h>

#define AZ(foo) assert((foo) == 0)
#define AN(foo) assert((foo) != 0)

#ifndef PATH_MAX
  #ifdef MAXPATHLEN
    #define PATH_MAX MAXPATHLEN
  #else
    #define PATH_MAX 1024
  #endif
#endif

#endif
