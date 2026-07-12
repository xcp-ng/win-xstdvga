// SPDX-License-Identifier: BSD-2-Clause

#include "bdd.hxx"

#pragma code_seg("PAGE")

NTSTATUS BASIC_DISPLAY_DRIVER::DetectIO() {
    PAGED_CODE();

    NTSTATUS Status;
    PHYSICAL_ADDRESS Bar2;
    ULONGLONG Bar2Size;

    Status = FindMemoryResource(1, (PULONGLONG)&Bar2.QuadPart, &Bar2Size);
    if (!NT_SUCCESS(Status)) {
        // may be legacy?
        BDD_LOG_TRACE("Failed to detect MMIO with status 0x%x", Status);
        return Status;
    }
    if (Bar2Size < 0x1000 || Bar2Size > ULONG_MAX) {
        BDD_LOG_ERROR("MMIO region size 0x%llx is invalid", Bar2Size);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Status = m_DxgkInterface.DxgkCbMapMemory(
        m_DxgkInterface.DeviceHandle,
        Bar2,
        (ULONG)Bar2Size,
        FALSE,
        FALSE,
        MmNonCached,
        &m_MappedBar2);
    if (!NT_SUCCESS(Status) || !m_MappedBar2) {
        BDD_LOG_ERROR("Mapping MMIO region (0x%p) failed with status 0x%x", m_MappedBar2, Status);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::StartHardware() {
    PAGED_CODE();

    NTSTATUS Status;
    ULONG VendorID;
    ULONG DeviceID;
    PHYSICAL_ADDRESS Framebuffer;
    ULONGLONG FramebufferBarSize;
    ULONG DispiMemory;

    PCI_COMMON_HEADER Header = {0};
    ULONG BytesRead;
    Status = m_DxgkInterface.DxgkCbReadDeviceSpace(
        m_DxgkInterface.DeviceHandle,
        DXGK_WHICHSPACE_CONFIG,
        &Header,
        0,
        sizeof(Header),
        &BytesRead);
    if (!NT_SUCCESS(Status) || BytesRead < sizeof(Header)) {
        BDD_LOG_ERROR("DxgkCbReadDeviceSpace failed with status 0x%x", Status);
        return Status;
    }

    VendorID = Header.VendorID;
    DeviceID = Header.DeviceID;
    if (Header.VendorID != 0x1234 || Header.DeviceID != 0x1111) {
        return STATUS_GRAPHICS_DRIVER_MISMATCH;
    }
    if (Header.BaseClass != PCI_CLASS_DISPLAY_CTLR) {
        return STATUS_GRAPHICS_DRIVER_MISMATCH;
    }
    switch (Header.SubClass) {
    case PCI_SUBCLASS_VID_VGA_CTLR:
    case PCI_SUBCLASS_VID_OTHER:
        break;
    default:
        return STATUS_GRAPHICS_DRIVER_MISMATCH;
    }

    Status = DetectIO();
    if (NT_SUCCESS(Status)) {
        m_Flags.Legacy = FALSE;
    } else if (Header.SubClass == PCI_SUBCLASS_VID_VGA_CTLR) {
        m_Flags.Legacy = TRUE;
    } else {
        BDD_LOG_ERROR("Failed to detect IO access method with status 0x%x", Status);
        return Status;
    }

    USHORT DispiId = DispiReadUShort(VBE_DISPI_INDEX_ID);
    BDD_LOG_TRACE("VBE DISPI version 0x%hx", DispiId);
    // needed for VBE_DISPI_INDEX_VIDEO_MEMORY_64K
    if (DispiId < VBE_DISPI_ID5 || DispiId > VBE_DISPI_ID_MAX) {
        Status = STATUS_GRAPHICS_DRIVER_MISMATCH;
        goto OutStopHardware;
    }

    Status = FindMemoryResource(0, (PULONGLONG)&Framebuffer.QuadPart, &FramebufferBarSize);
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("Failed to detect framebuffer with Status: 0x%x", Status);
        goto OutStopHardware;
    }
    BDD_LOG_TRACE("Detected framebuffer BAR at 0x%llx+0x%llx", Framebuffer.QuadPart, FramebufferBarSize);

    m_VbeInfo.Framebuffer = Framebuffer;
    DispiMemory = (ULONG)DispiReadUShort(VBE_DISPI_INDEX_VIDEO_MEMORY_64K) * 64 * 1024;
    if (DispiMemory > FramebufferBarSize) {
        BDD_LOG_WARNING(
            "DISPI reported video memory (%lu) is larger than framebuffer BAR (%llu)",
            DispiMemory,
            FramebufferBarSize);
        m_VbeInfo.VideoMemory = (ULONG)FramebufferBarSize;
    } else {
        m_VbeInfo.VideoMemory = DispiMemory;
    }

    DispiWriteUShort(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED | VBE_DISPI_GETCAPS);
    if (DispiReadUShort(VBE_DISPI_INDEX_ENABLE) == (VBE_DISPI_DISABLED | VBE_DISPI_GETCAPS)) {
        m_VbeInfo.MaxXres = DispiReadUShort(VBE_DISPI_INDEX_XRES);
        m_VbeInfo.MaxYres = DispiReadUShort(VBE_DISPI_INDEX_YRES);
        m_VbeInfo.MaxBpp = DispiReadUShort(VBE_DISPI_INDEX_BPP);

        if (m_VbeInfo.MaxXres < 640 || m_VbeInfo.MaxYres < 480 || m_VbeInfo.MaxBpp < BPP) {
            BDD_LOG_ERROR(
                "DISPI reported capability %hux%hux%hu is not supported (unresponsive device?)",
                m_VbeInfo.MaxXres,
                m_VbeInfo.MaxYres,
                m_VbeInfo.MaxBpp);
            Status = STATUS_NOT_SUPPORTED;
            goto OutDisableDispi;
        } else {
            BDD_LOG_TRACE(
                "DISPI reported capability %hux%hux%hu",
                m_VbeInfo.MaxXres,
                m_VbeInfo.MaxYres,
                m_VbeInfo.MaxBpp);
        }
    } else {
        m_VbeInfo.MaxXres = m_VbeInfo.MaxYres = USHORT_MAX;
        m_VbeInfo.MaxBpp = BPP;
    }
    DispiWriteUShort(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);

    return STATUS_SUCCESS;

OutDisableDispi:
    DispiWriteUShort(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);

OutStopHardware:
    StopHardware();

    return Status;
}

NTSTATUS BASIC_DISPLAY_DRIVER::StopHardware() {
    PAGED_CODE();

    NTSTATUS Status;

    if (!m_Flags.Legacy && m_MappedBar2) {
        Status = m_DxgkInterface.DxgkCbUnmapMemory(m_DxgkInterface.DeviceHandle, m_MappedBar2);
        if (!NT_SUCCESS(Status)) {
            BDD_LOG_ERROR("DxgkCbUnmapMemory(m_MappedBar2) failed with status 0x%x", Status);
        }
        m_MappedBar2 = NULL;
    }

    RtlZeroMemory(&m_VbeInfo, sizeof(m_VbeInfo));

    return STATUS_SUCCESS;
}

NTSTATUS
BASIC_DISPLAY_DRIVER::FindMemoryResource(_In_ ULONG Index, _Out_ PULONGLONG Start, _Out_ PULONGLONG Size) {
    PAGED_CODE();

    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG FoundCount = 0;

    if (!Start || !Size) {
        return STATUS_INVALID_PARAMETER;
    }

    *Start = 0;
    *Size = 0;

    if (!m_DeviceInfo.TranslatedResourceList) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Full = m_DeviceInfo.TranslatedResourceList->List;
    for (ULONG i = 0; i < m_DeviceInfo.TranslatedResourceList->Count; i++) {
        for (ULONG j = 0; j < Full->PartialResourceList.Count; ++j) {
            ULONGLONG ThisStart, ThisSize;

            Partial = &Full->PartialResourceList.PartialDescriptors[j];

            if (Partial->Type != CmResourceTypeMemory && Partial->Type != CmResourceTypeMemoryLarge) {
                continue;
            }

            ThisSize = RtlCmDecodeMemIoResource(Partial, &ThisStart);
            BDD_LOG_INFO("Found memory at 0x%llx+0x%llx", ThisStart, ThisSize);

            if (FoundCount++ == Index) {
                if (ThisSize == 0) {
                    return STATUS_DEVICE_CONFIGURATION_ERROR;
                }
                *Start = ThisStart;
                *Size = ThisSize;
                return STATUS_SUCCESS;
            }
        }
        Full = (PCM_FULL_RESOURCE_DESCRIPTOR)(Full->PartialResourceList.PartialDescriptors +
                                              Full->PartialResourceList.Count);
    }

    return STATUS_DEVICE_CONFIGURATION_ERROR;
}
