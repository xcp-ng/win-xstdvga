// SPDX-License-Identifier: BSD-2-Clause

#include "bdd.hxx"

#pragma code_seg("PAGE")

static const BYTE EDIDTemplate[] = {
    // header
    0x00,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0xFF,
    0x00,
    // vendor ID "RHT"
    0x49,
    0x14,
    // model
    0x00,
    0x00,
    // serial (to use TargetId)
    0x00,
    0x00,
    0x00,
    0x00,
    // model year 2026
    0xFF,
    0x24,
    // EDID 1.4
    0x01,
    0x04,
    // digital, 8bpc, undefined interface
    0xA0,
    // screen size not defined
    0x00,
    0x00,
    // gamma 2.2
    0x78,
    // active off, RGB 4:4:4, sRGB, preferred timing mode, multimode
    0x26,
    // sRGB chromaticity
    0xEE,
    0x91,
    0xA3,
    0x54,
    0x4C,
    0x99,
    0x26,
    0x0F,
    0x50,
    0x54,
    // established timings (Device.Display.Monitor.Modes)
    0x21,
    0x08,
    // manufacturer timings
    0x00,

    // standard timings (all reserved)
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,
    0x01,

    // preferred timing mode 0.79M3-R (CVT 1.2a)
    // 1) Enter Desired Horizontal Pixels Here (I_H_PIXELS) =>                     1024
    // 2) Enter Desired Vertical Lines Here (I_V_LINES) =>                         768
    // 3) Enter (Y or N) If You Want Margins  =>                                   n
    // 4) Enter (Y or N) If You Want Interlace  =>                                 n
    // 5) Enter Vertical Scan Frame Rate Here (I_IP_FREQ_RDQ) =>                   60
    // 6) Enter (Y or N) If You Want Reduced Blanking Here   =>                    y
    // 7) Use Reduced Blank (RB) Timing version  (1)  rules (I_RED_BLANK_VER) =>   1
    // 56 MHz
    0xE0,
    0x15,
    // horizontal (1024 addressable, 160 blank)
    0x00,
    0xA0,
    0x40,
    // vertical (768 addressable, 22 blank)
    0x00,
    0x16,
    0x30,
    // HFP (48px)
    0x30,
    // HSPW (32px)
    0x20,
    // VFP/VSPW (3/4)
    0x34,
    // HFP/HSPW/VFP/VSPW upper bits
    0x00,
    // image size
    0x00,
    0x00,
    0x00,
    // borders
    0x00,
    0x00,
    // non-interlaced, no stereo, digital +HSync -VSync
    0x1A,

    // display descriptor
    0x00,
    0x00,
    0x00,
    // display product name
    0xFC,
    0x00,
    // name
    'X',
    'S',
    'T',
    'D',
    'V',
    'G',
    'A',
    '\n',
    ' ',
    ' ',
    ' ',
    ' ',
    ' ',

    // dummy descriptor
    0x00,
    0x00,
    0x00,
    0x10,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,

    // dummy descriptor
    0x00,
    0x00,
    0x00,
    0x10,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,

    // no extension
    0x00,

    // checksum (keep at zero, to be calculated later by GetEdid)
    0x00,
};

NTSTATUS
BASIC_DISPLAY_DRIVER::GetEdid(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId) {
    PAGED_CODE();

    static_assert(sizeof(EDIDTemplate) == EDID_V1_BLOCK_SIZE);
    BDD_ASSERT_CHK(!m_Flags.EDID_Attempted);

    NTSTATUS Status = STATUS_SUCCESS;
    PBYTE Edid = m_EDIDs[TargetId];
    BYTE Checksum = 0;

    RtlCopyMemory(Edid, EDIDTemplate, EDID_V1_BLOCK_SIZE);
    // HACK: trickery to get an unique serial number
    RtlCopyMemory(Edid + 0xC, &TargetId, 4);
    for (UINT i = 0; i < EDID_V1_BLOCK_SIZE; i++) {
        Checksum += Edid[i];
    }
    Edid[EDID_V1_BLOCK_SIZE - 1] = -Checksum;

    m_Flags.EDID_Attempted = TRUE;
    m_Flags.EDID_Retrieved = TRUE;
    m_Flags.EDID_ValidHeader = TRUE;
    m_Flags.EDID_ValidChecksum = TRUE;

    return Status;
}
