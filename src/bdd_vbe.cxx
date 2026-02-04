// SPDX-License-Identifier: BSD-2-Clause

#include "bdd.hxx"

#pragma code_seg("PAGE")

#define BPP 32

CONST BDD_VBE_STANDARD_RESOLUTION BddVbeStandardResolutions[BDD_VBE_STANDARD_RESOLUTION_COUNT] = {
    {640, 480},   //
    {800, 480},   //
    {800, 600},   //
    {832, 624},   //
    {960, 640},   //
    {1024, 600},  //
    {1024, 768},  //
    {1152, 864},  //
    {1152, 870},  //
    {1280, 720},  //
    {1280, 760},  //
    {1280, 768},  //
    {1280, 800},  //
    {1280, 960},  //
    {1280, 1024}, //
    {1360, 768},  //
    {1366, 768},  //
    {1400, 1050}, //
    {1440, 900},  //
    {1600, 900},  //
    {1600, 1200}, //
    {1680, 1050}, //
    {1920, 1080}, //
    {1920, 1200}, //
    {1920, 1440}, //
    {2000, 2000}, //
    {2048, 1536}, //
    {2048, 2048}, //
    {2560, 1440}, //
    {2560, 1600}, //
    {2560, 2048}, //
    {2800, 2100}, //
    {3200, 2400}, //
    {3840, 2160}, //
    {4096, 2160}, //
    {7680, 4320}, //
    {8192, 4320}, //
};

NTSTATUS BASIC_DISPLAY_DRIVER::EnumerateVBE(_In_opt_ PDXGK_DISPLAY_INFORMATION PostDisplayInfo) {
    PAGED_CODE();

    PHYSICAL_ADDRESS Framebuffer;
    ULONGLONG FramebufferSize;
    NTSTATUS Status;

    m_ModeInfo.Count = 0;

    Status = FindMemoryResource(0, (PULONGLONG)&Framebuffer.QuadPart, &FramebufferSize);
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("Failed to detect framebuffer with Status: 0x%x", Status);
        return Status;
    }
    BDD_LOG_INFO("Detected framebuffer BAR at 0x%llx+0x%llx", Framebuffer.QuadPart, FramebufferSize);

    ULONG DispiMemory = (ULONG)DispiReadUShort(VBE_DISPI_INDEX_VIDEO_MEMORY_64K) * 64 * 1024;
    if (DispiMemory > FramebufferSize) {
        BDD_LOG_WARNING(
            "DISPI reported video memory (%lu) is larger than framebuffer BAR (%llu)",
            DispiMemory,
            FramebufferSize);
    } else {
        FramebufferSize = DispiMemory;
    }

    if (PostDisplayInfo != NULL && PostDisplayInfo->Width != 0) {
        UINT PostBPP = BPPFromPixelFormat(PostDisplayInfo->ColorFormat);

        if (PostBPP == BPP) {
            BDD_VBE_MODE *pBddMode = &m_ModeInfo.Modes[m_ModeInfo.Count];

            pBddMode->Width = (USHORT)PostDisplayInfo->Width;
            pBddMode->Height = (USHORT)PostDisplayInfo->Height;
            pBddMode->BitsPerPixel = (USHORT)PostBPP;
            pBddMode->Pitch = (USHORT)PostDisplayInfo->Pitch;
            pBddMode->PhysicalAddress = PostDisplayInfo->PhysicAddress;
            pBddMode->ModeNumber = m_ModeInfo.Count;

            BDD_LOG_TRACE(
                "Adding POST resolution %hux%hu as mode %hu",
                pBddMode->Width,
                pBddMode->Height,
                pBddMode->ModeNumber);

            m_ModeInfo.Count++;
        }
    }

    for (UINT i = 0; i < ARRAYSIZE(BddVbeStandardResolutions) && m_ModeInfo.Count < ARRAYSIZE(m_ModeInfo.Modes); i++) {
        CONST BDD_VBE_STANDARD_RESOLUTION *Resolution = &BddVbeStandardResolutions[i];

        if (Resolution->Width % 8 != 0 || Resolution->Height % 8 != 0) {
            // filter out resolutions that don't work very well (1366x768, 1400x1050 etc.)
            BDD_LOG_INFO("Skipped standard resolution %hux%hu (ratio)", Resolution->Width, Resolution->Height);
            continue;
        }

        ULONG RequiredMemory = (ULONG)Resolution->Width * Resolution->Height * (BPP / BITS_PER_BYTE);
        if (RequiredMemory == 0 || RequiredMemory > FramebufferSize) {
            BDD_LOG_INFO("Skipped standard resolution %hux%hu (too big)", Resolution->Width, Resolution->Height);
            continue;
        }

        // was the POST mode created?
        if (m_ModeInfo.Count > 0 &&                             //
            m_ModeInfo.Modes[0].Width == Resolution->Width &&   //
            m_ModeInfo.Modes[0].Height == Resolution->Height && //
            m_ModeInfo.Modes[0].BitsPerPixel == BPP) {
            continue;
        }

        BDD_VBE_MODE *pBddMode = &m_ModeInfo.Modes[m_ModeInfo.Count];

        pBddMode->Width = Resolution->Width;
        pBddMode->Height = Resolution->Height;
        pBddMode->BitsPerPixel = (USHORT)BPP;
        pBddMode->Pitch = (pBddMode->Width * pBddMode->BitsPerPixel) / BITS_PER_BYTE;
        pBddMode->PhysicalAddress = Framebuffer;
        pBddMode->ModeNumber = m_ModeInfo.Count;

        BDD_LOG_TRACE(
            "Adding standard resolution %hux%hu as mode %hu",
            Resolution->Width,
            Resolution->Height,
            pBddMode->ModeNumber);

        m_ModeInfo.Count++;
    }

    BDD_LOG_TRACE("Added %hu modes", m_ModeInfo.Count);
    if (m_ModeInfo.Count == 0) {
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::SetVBEMode(USHORT ModeNumber) {
    PAGED_CODE();

    if (ModeNumber >= m_ModeInfo.Count) {
        return STATUS_INVALID_PARAMETER;
    }

    USHORT Width = m_ModeInfo.Modes[ModeNumber].Width;
    USHORT Height = m_ModeInfo.Modes[ModeNumber].Height;
    USHORT Bpp = m_ModeInfo.Modes[ModeNumber].BitsPerPixel;

    DispiWriteUShort(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    DispiWriteUShort(VBE_DISPI_INDEX_BANK, 0);
    DispiWriteUShort(VBE_DISPI_INDEX_X_OFFSET, 0);
    DispiWriteUShort(VBE_DISPI_INDEX_Y_OFFSET, 0);

    DispiWriteUShort(VBE_DISPI_INDEX_BPP, Bpp);
    DispiWriteUShort(VBE_DISPI_INDEX_XRES, Width);
    DispiWriteUShort(VBE_DISPI_INDEX_VIRT_WIDTH, Width);
    DispiWriteUShort(VBE_DISPI_INDEX_YRES, Height);
    DispiWriteUShort(VBE_DISPI_INDEX_VIRT_HEIGHT, Height);

    DispiWriteUShort(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    return STATUS_SUCCESS;
}
