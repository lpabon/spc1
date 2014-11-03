//
// Copyright (c) 2014 The tm Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.


#ifndef _TM_H
#define _TM_H

#include <time.h>

#define TM_NOW(t)  clock_gettime(CLOCK_MONOTONIC, &(t)) 
#define TM_DURATION_NSEC(te, ts) (((te).tv_sec-(ts).tv_sec)*(int64_t)1e9) + \
    ((te).tv_nsec-(ts).tv_nsec)
#define TM_DURATION_N2USEC(d) ((d)/(int64_t)1000) 
#define TM_DURATION_N2MSEC(d) (TM_DURATION_N2USEC((d))/(int64_t)1000) 
#define TM_DURATION_N2SEC(d) (TM_DURATION_N2MSEC((d))/(int64_t)1000) 

#endif
