#ifndef __LISAINFERENCE_COMMON_H__
#define __LISAINFERENCE_COMMON_H__ 1

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <float.h>
#include <stdbool.h>

#include "LISAutils.h"

#if defined(__cplusplus)
extern "C" {
#define complex _Complex
#elif 0
} /* so that editors will match preceding brace */
#endif

void addendum(int argc, char *argv[],LISARunParams *runParams, int *ndim, int *nPar,int **freeparamsmapp,void **contextp, double *logZtrue);

#if 0
{ /* so that editors will match succeeding brace */
#elif defined(__cplusplus)
}
#endif

#endif
