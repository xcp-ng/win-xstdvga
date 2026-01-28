// SPDX-License-Identifier: BSD-2-Clause

#pragma once

extern "C" {
#include <ntddk.h>
};

#include "vbe_qemu.hxx"

#define BDD_VBE_STANDARD_RESOLUTION_COUNT 37

typedef struct _BDD_VBE_STANDARD_RESOLUTION {
    USHORT Width;
    USHORT Height;
} BDD_VBE_STANDARD_RESOLUTION;

typedef struct _BDD_VBE_MODE {
    USHORT Width;
    USHORT Height;
    USHORT Pitch;
    USHORT BitsPerPixel;
    PHYSICAL_ADDRESS PhysicalAddress;
    USHORT ModeNumber;
} BDD_VBE_MODE;

extern const BDD_VBE_STANDARD_RESOLUTION BddVbeStandardResolutions[BDD_VBE_STANDARD_RESOLUTION_COUNT];

typedef struct _BDD_MODE_INFO {
    USHORT Count;
    BDD_VBE_MODE Modes[BDD_VBE_STANDARD_RESOLUTION_COUNT];
} BDD_MODE_INFO;
