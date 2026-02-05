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
    // serial (to be filled in by GetEdid)
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
    // no established timings; see the PTM below to see why
    0x00,
    0x00,
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

    // preferred timing mode using CVT 1.2a calculator:
    // 1) Enter Desired Horizontal Pixels Here (I_H_PIXELS) =>                     1152
    // 2) Enter Desired Vertical Lines Here (I_V_LINES) =>                         864
    // 3) Enter (Y or N) If You Want Margins  =>                                   n
    // 4) Enter (Y or N) If You Want Interlace  =>                                 n
    // 5) Enter Vertical Scan Frame Rate Here (I_IP_FREQ_RDQ) =>                   60
    // 6) Enter (Y or N) If You Want Reduced Blanking Here   =>                    y
    // 7) Use Reduced Blank (RB) Timing version  (1)  rules (I_RED_BLANK_VER) =>   1
    // The HLK Server 2019 test "Verify VESA and CEA required display modes" requires that if we report a standard VESA
    // DMT mode in EDID, then the adapter-reported refresh rate must match that of the reported mode. However our KMDOD
    // reports the sync frequencies as D3DKMDT_FREQUENCY_NOTSPECIFIED, which causes us to fail the test. 1152x864@60Hz
    // is a reasonably-sized mode that is not in DMT (the one in DMT is 75Hz), so choose that instead of the more
    // pedestrian 1024x768@60Hz.
    // Note that we also can't report any established timings for the same reason.
    0x3F,
    0x1B,
    0x80,
    0xA0,
    0x40,
    0x60,
    0x19,
    0x30,
    0x30,
    0x20,
    0x34,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
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

static ULONG GetSerialNumber(PDEVICE_OBJECT Pdo, D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId) {
    PAGED_CODE();

    ULONG Bus, Address, Device, Function;
    ULONG Length;
    NTSTATUS Status;

    if (!Pdo) {
        return TargetId & 0xFFFF;
    }

    Status = IoGetDeviceProperty(Pdo, DevicePropertyBusNumber, sizeof(Bus), &Bus, &Length);
    if (!NT_SUCCESS(Status)) {
        Bus = 0;
    }
    Bus &= 0xFF;

    Status = IoGetDeviceProperty(Pdo, DevicePropertyAddress, sizeof(Address), &Address, &Length);
    if (!NT_SUCCESS(Status)) {
        Address = 0;
    }
    Device = (Address >> 16) & 0x1F;
    Function = Address & 0x7;

    return (Bus << 24) | (Device << 19) | (Function << 16) | (TargetId & 0xFFFF);
}

NTSTATUS
BASIC_DISPLAY_DRIVER::GetEdid(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId) {
    PAGED_CODE();

    static_assert(sizeof(EDIDTemplate) == EDID_V1_BLOCK_SIZE);
    BDD_ASSERT_CHK(!m_Flags.EDID_Attempted);

    NTSTATUS Status = STATUS_SUCCESS;
    PBYTE Edid = m_EDIDs[TargetId];
    BYTE Checksum = 0;
    ULONG SerialNumber = GetSerialNumber(m_DeviceInfo.PhysicalDeviceObject, TargetId);

    RtlCopyMemory(Edid, EDIDTemplate, EDID_V1_BLOCK_SIZE);
    RtlCopyMemory(Edid + 0xC, &SerialNumber, sizeof(SerialNumber));
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
