// SPDX-FileCopyrightText: 2021 Jorrit Rouwe
// SPDX-License-Identifier: MIT

#include <Jolt/Jolt.h>

#include <Jolt/Physics/PhysicsLock.h>

#ifdef JPH_ENABLE_ASSERTS

JPH_NAMESPACE_BEGIN

thread_local uint32 PhysicsLock::sLockedMutexes = 0;

JPH_NAMESPACE_END

#endif
