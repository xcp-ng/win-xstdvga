// SPDX-License-Identifier: BSD-2-Clause

#include "bdd.hxx"

#pragma code_seg("PAGE")

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

NTSTATUS
BASIC_DISPLAY_DRIVER::AddVBEMode(
    USHORT Width,
    USHORT Height,
    USHORT Bpp,
    _In_opt_ CONST PHYSICAL_ADDRESS *PhysicalAddress) {
    PAGED_CODE();

    if (Bpp != BPP) {
        BDD_LOG_INFO("Skipped mode %hux%hux%hu (unsupported BPP)", Width, Height, Bpp);
        return STATUS_NOT_SUPPORTED;
    }

    if (Width > m_VbeInfo.MaxXres || Height > m_VbeInfo.MaxYres || Bpp > m_VbeInfo.MaxBpp) {
        BDD_LOG_INFO("Skipped mode %hux%hux%hu (too big for device)", Width, Height, Bpp);
        return STATUS_NOT_SUPPORTED;
    }

    ULONG RequiredMemory = (ULONG)Width * Height * (Bpp / BITS_PER_BYTE);
    if (RequiredMemory == 0 || RequiredMemory > m_VbeInfo.VideoMemory) {
        BDD_LOG_INFO("Skipped mode %hux%hux%hu (too big for video memory)", Width, Height, Bpp);
        return STATUS_NOT_SUPPORTED;
    }

    if (Width % 8 != 0 || Height % 8 != 0) {
        // filter out resolutions that don't work very well (1366x768, 1400x1050 etc.)
        BDD_LOG_INFO("Skipped mode %hux%hux%hu (ratio)", Width, Height, Bpp);
        return STATUS_NOT_SUPPORTED;
    }

    // was the POST mode created?
    if (m_VbeInfo.ModeCount > 0 &&             //
        m_VbeInfo.Modes[0].Width == Width &&   //
        m_VbeInfo.Modes[0].Height == Height && //
        m_VbeInfo.Modes[0].BitsPerPixel == Bpp) {
        return STATUS_NOT_SUPPORTED;
    }

    PBDD_VBE_MODE pBddMode = &m_VbeInfo.Modes[m_VbeInfo.ModeCount];

    pBddMode->Width = Width;
    pBddMode->Height = Height;
    pBddMode->BitsPerPixel = Bpp;
    pBddMode->Pitch = (pBddMode->Width * pBddMode->BitsPerPixel) / BITS_PER_BYTE;
    if (PhysicalAddress) {
        pBddMode->PhysicalAddress = *PhysicalAddress;
    } else {
        pBddMode->PhysicalAddress = m_VbeInfo.Framebuffer;
    }
    pBddMode->ModeNumber = m_VbeInfo.ModeCount;

    BDD_LOG_TRACE("Adding mode %hux%hux%hu as number %hu", Width, Height, Bpp, pBddMode->ModeNumber);

    m_VbeInfo.ModeCount++;

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::EnumerateVBE(_In_opt_ PDXGK_DISPLAY_INFORMATION PostDisplayInfo) {
    PAGED_CODE();

    NTSTATUS Status;

    m_VbeInfo.ModeCount = 0;

    if (PostDisplayInfo != NULL && PostDisplayInfo->Width != 0) {
        Status = AddVBEMode(
            (USHORT)PostDisplayInfo->Width,
            (USHORT)PostDisplayInfo->Height,
            (USHORT)BPPFromPixelFormat(PostDisplayInfo->ColorFormat),
            &PostDisplayInfo->PhysicAddress);
        if (!NT_SUCCESS(Status)) {
            BDD_LOG_ERROR("Failed to add POST mode with status 0x%x", Status);
        }
    }

    for (UINT i = 0; i < ARRAYSIZE(BddVbeStandardResolutions) && m_VbeInfo.ModeCount < ARRAYSIZE(m_VbeInfo.Modes);
         i++) {
        Status = AddVBEMode(BddVbeStandardResolutions[i].Width, BddVbeStandardResolutions[i].Height, BPP);
        if (!NT_SUCCESS(Status)) {
            BDD_LOG_INFO("Failed to add standard mode %u with status 0x%x", i, Status);
        }
    }

    BDD_LOG_TRACE("Added %hu modes", m_VbeInfo.ModeCount);
    if (m_VbeInfo.ModeCount == 0) {
        BDD_LOG_ERROR("No modes added");
        return STATUS_UNSUCCESSFUL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::SetVBEMode(USHORT ModeNumber) {
    PAGED_CODE();

    if (ModeNumber >= m_VbeInfo.ModeCount) {
        return STATUS_INVALID_PARAMETER;
    }

    USHORT Width = m_VbeInfo.Modes[ModeNumber].Width;
    USHORT Height = m_VbeInfo.Modes[ModeNumber].Height;
    USHORT Bpp = m_VbeInfo.Modes[ModeNumber].BitsPerPixel;

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
