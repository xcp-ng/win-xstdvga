// SPDX-License-Identifier: MS-PL

// Based on the Microsoft KMDOD example
// Copyright (c) 2010 Microsoft Corporation

#include "bdd.hxx"

#pragma code_seg("PAGE")

//
// New and delete operators
//
_When_(
    (PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError(
        "Must succeed pool allocations are forbidden. "
        "Allocation failures cause a system crash")) void *__cdecl
operator new(size_t Size, POOL_TYPE PoolType) {
    PAGED_CODE();

    Size = (Size != 0) ? Size : 1;

    void *pObject = ExAllocatePoolZero(PoolType, Size, BDDTAG);

#if DBG
    if (pObject != NULL) {
        RtlFillMemory(pObject, Size, 0xCD);
    }
#endif // DBG

    return pObject;
}

_When_(
    (PoolType & NonPagedPoolMustSucceed) != 0,
    __drv_reportError(
        "Must succeed pool allocations are forbidden. "
        "Allocation failures cause a system crash")) void *__cdecl
operator new[](size_t Size, POOL_TYPE PoolType) {
    PAGED_CODE();

    Size = (Size != 0) ? Size : 1;

    void *pObject = ExAllocatePoolZero(PoolType, Size, BDDTAG);

#if DBG
    if (pObject != NULL) {
        RtlFillMemory(pObject, Size, 0xCD);
    }
#endif // DBG

    return pObject;
}

void __cdecl operator delete(void *pObject) {
    PAGED_CODE();

    if (pObject != NULL) {
        ExFreePool(pObject);
    }
}

//
// size_t version is needed for VS2015(C++ 14).
//
void __cdecl operator delete(void *pObject, size_t s) {
    PAGED_CODE();

    UNREFERENCED_PARAMETER(s);

    ::operator delete(pObject);
}

void __cdecl operator delete[](void *pObject) {
    PAGED_CODE();

    if (pObject != NULL) {
        ExFreePool(pObject);
    }
}
