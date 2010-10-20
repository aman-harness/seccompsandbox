// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "debug.h"
#include "sandbox_impl.h"

namespace playground {

long Sandbox::sandbox_madvise(void* start, size_t length, int advice) {
  long long tm;
  Debug::syscall(&tm, __NR_madvise, "Executing handler");
  struct {
    struct RequestHeader header;
    MAdvise   madvise_req;
  } __attribute__((packed)) request;
  request.madvise_req.start  = start;
  request.madvise_req.len    = length;
  request.madvise_req.advice = advice;

  long rc = forwardSyscall(__NR_madvise, &request.header, sizeof(request));
  Debug::elapsed(tm, __NR_madvise);
  return rc;
}

bool Sandbox::process_madvise(const SecureMem::SyscallRequestInfo* info) {
  // Read request
  MAdvise madvise_req;
  SysCalls sys;
  if (read(sys, info->trustedProcessFd, &madvise_req, sizeof(madvise_req)) !=
      sizeof(madvise_req)) {
    die("Failed to read parameters for madvise() [process]");
  }
  int rc = -EINVAL;
  switch (madvise_req.advice) {
    case MADV_NORMAL:
    case MADV_RANDOM:
    case MADV_SEQUENTIAL:
    case MADV_WILLNEED:
    ok:
      SecureMem::sendSystemCall(*info, SecureMem::SEND_UNLOCKED,
                                madvise_req.start, madvise_req.len,
                                madvise_req.advice);
      return true;
    default:
      // All other flags to madvise() are potential dangerous (as opposed to
      // merely affecting overall performance). Do not allow them on memory
      // ranges that were part of the original mappings.
      void *stop = reinterpret_cast<void *>(
          (char *)madvise_req.start + madvise_req.len);
      ProtectedMap::const_iterator iter = protectedMap_.lower_bound(
          (void *)madvise_req.start);
      if (iter != protectedMap_.begin()) {
        --iter;
      }
      for (; iter != protectedMap_.end() && iter->first < stop; ++iter) {
        if (madvise_req.start < reinterpret_cast<void *>(
                reinterpret_cast<char *>(iter->first) + iter->second) &&
            stop > iter->first) {
          SecureMem::abandonSystemCall(*info, rc);
          return false;
        }
      }

      // Changing attributes on memory regions that were newly mapped inside of
      // the sandbox is OK.
      goto ok;
  }
}

} // namespace
