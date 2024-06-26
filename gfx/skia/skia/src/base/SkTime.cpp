/*
 * Copyright 2023 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "src/base/SkTime.h"

#if !defined(__has_feature)
    #define  __has_feature(x) 0
#endif

#if __has_feature(memory_sanitizer) || defined(SK_BUILD_FOR_UNIX) || defined(SK_BUILD_FOR_ANDROID)
#include <time.h>
double SkTime::GetNSecs() {
    // See skia:6504
    struct timespec tp;
    clock_gettime(CLOCK_MONOTONIC, &tp);
    return tp.tv_sec * 1e9 + tp.tv_nsec;
}
#else
#include <chrono>
#include <ratio>
double SkTime::GetNSecs() {
    auto now = std::chrono::steady_clock::now();
    std::chrono::duration<double, std::nano> ns = now.time_since_epoch();
    return ns.count();
}
#endif
