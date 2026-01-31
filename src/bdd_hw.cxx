// SPDX-License-Identifier: BSD-2-Clause

#include "bdd.hxx"

#pragma code_seg("PAGE")

NTSTATUS BASIC_DISPLAY_DRIVER::StartHardware() {
    PAGED_CODE();

    NTSTATUS Status;
    ULONG VendorID;
    ULONG DeviceID;

    // Get the Vendor & Device IDs on PCI system
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

    PHYSICAL_ADDRESS Bar2;
    ULONGLONG Bar2Size;

    Status = FindMemoryResource(1, (PULONGLONG)&Bar2.QuadPart, &Bar2Size);
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("Failed to detect MMIO with status 0x%x", Status);
        return Status;
    }
    if (Bar2Size < 0x1000 || Bar2Size > ULONG_MAX) {
        BDD_LOG_ERROR("MMIO region size 0x%llx is invalid", Bar2Size);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    // confusingly enough, DxgkCbMapMemory with InIoSpace=TRUE refers to PIO "space" rather than IO memory space
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

    USHORT DispiId = DispiReadUShort(VBE_DISPI_INDEX_ID);
    BDD_LOG_INFORMATION("VBE version 0x%hx", DispiId);
    // the only version supported by QEMU, needed for VBE_DISPI_INDEX_VIDEO_MEMORY_64K
    if (DispiId != VBE_DISPI_ID5) {
        Status = STATUS_GRAPHICS_DRIVER_MISMATCH;
        goto OutStopHardware;
    }

    return STATUS_SUCCESS;

OutStopHardware:
    StopHardware();

    return Status;
}

NTSTATUS BASIC_DISPLAY_DRIVER::StopHardware() {
    PAGED_CODE();

    NTSTATUS Status;

    if (m_MappedBar2) {
        Status = m_DxgkInterface.DxgkCbUnmapMemory(m_DxgkInterface.DeviceHandle, m_MappedBar2);
        if (!NT_SUCCESS(Status)) {
            BDD_LOG_ERROR("DxgkCbUnmapMemory(m_MappedBar2) failed with status 0x%x", Status);
        }
        m_MappedBar2 = NULL;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BASIC_DISPLAY_DRIVER::FindMemoryResource(_In_ ULONG Index, _Out_opt_ PULONGLONG Start, _Out_ PULONGLONG Size) {
    PAGED_CODE();

    PCM_FULL_RESOURCE_DESCRIPTOR Full;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Partial;
    ULONG Found = 0;

    *Size = 0;
    if (Start) {
        *Start = 0;
    }

    for (ULONG i = 0; i < m_DeviceInfo.TranslatedResourceList->Count; i++) {
        Full = &m_DeviceInfo.TranslatedResourceList->List[i];

        for (ULONG j = 0; j < Full->PartialResourceList.Count; ++j) {
            Partial = &Full->PartialResourceList.PartialDescriptors[j];

            if (Partial->Type != CmResourceTypeMemory && Partial->Type != CmResourceTypeMemoryLarge) {
                continue;
            }

            if (Found++ == Index) {
                *Size = RtlCmDecodeMemIoResource(Partial, Start);
                break;
            }
        }
        if (*Size != 0) {
            break;
        }
    }

    if (*Size == 0) {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    return STATUS_SUCCESS;
}
