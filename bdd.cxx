// SPDX-License-Identifier: MS-PL

// Based on the Microsoft KMDOD example
// Copyright (c) 2010 Microsoft Corporation
// Copyright 2026 Vates.

#include "bdd.hxx"

#pragma code_seg("PAGE")

BASIC_DISPLAY_DRIVER::BASIC_DISPLAY_DRIVER(_In_ DEVICE_OBJECT *pPhysicalDeviceObject)
    : m_pPhysicalDevice(pPhysicalDeviceObject), //
      m_MappedBar2(NULL),                       //
      m_MonitorPowerState(PowerDeviceD0),       //
      m_AdapterPowerState(PowerDeviceD0),       //
      m_SystemDisplaySourceId(D3DDDI_ID_UNINITIALIZED) {
    PAGED_CODE();
    *((UINT *)&m_Flags) = 0;
    m_Flags._LastFlag = TRUE;
    RtlZeroMemory(&m_DxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(&m_StartInfo, sizeof(m_StartInfo));
    RtlZeroMemory(&m_EDIDs, sizeof(m_EDIDs));
    RtlZeroMemory(&m_CurrentModes, sizeof(m_CurrentModes));
    RtlZeroMemory(&m_DeviceInfo, sizeof(m_DeviceInfo));
    RtlZeroMemory(&m_ModeInfo, sizeof(m_ModeInfo));

    for (UINT i = 0; i < MAX_VIEWS; i++) {
        m_HardwareBlt[i].Initialize(this, i);
    }
}

BASIC_DISPLAY_DRIVER::~BASIC_DISPLAY_DRIVER() {
    PAGED_CODE();

    CleanUp();
}

NTSTATUS BASIC_DISPLAY_DRIVER::StartDevice(
    _In_ DXGK_START_INFO *pDxgkStartInfo,
    _In_ DXGKRNL_INTERFACE *pDxgkInterface,
    _Out_ ULONG *pNumberOfViews,
    _Out_ ULONG *pNumberOfChildren) {
    PAGED_CODE();

    BDD_ASSERT(pDxgkStartInfo != NULL);
    BDD_ASSERT(pDxgkInterface != NULL);
    BDD_ASSERT(pNumberOfViews != NULL);
    BDD_ASSERT(pNumberOfChildren != NULL);

    RtlCopyMemory(&m_StartInfo, pDxgkStartInfo, sizeof(m_StartInfo));
    RtlCopyMemory(&m_DxgkInterface, pDxgkInterface, sizeof(m_DxgkInterface));
    RtlZeroMemory(m_CurrentModes, sizeof(m_CurrentModes));
    m_CurrentModes[0].DispInfo.TargetId = D3DDDI_ID_UNINITIALIZED;

    // Get device information from OS.
    NTSTATUS Status = m_DxgkInterface.DxgkCbGetDeviceInformation(m_DxgkInterface.DeviceHandle, &m_DeviceInfo);
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ASSERTION("DxgkCbGetDeviceInformation failed with status 0x%x", Status);
        return Status;
    }

    // Ignore return value, since it's not the end of the world if we failed to write these values to the registry
    RegisterHWInfo();

    Status = StartHardware();
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("StartHardware failed with status 0x%x", Status);
        return Status;
    }

    // we just need the current mode, the framebuffer will be taken from the BAR
    Status =
        m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(m_DxgkInterface.DeviceHandle, &m_CurrentModes[0].DispInfo);
    if (NT_SUCCESS(Status) && m_CurrentModes[0].DispInfo.Width != 0) {
        m_Flags.HasPostDisplay = TRUE;
    } else {
        // The most likely cause of failure is that the driver is simply not running on a POST device, or we are running
        // after a pre-WDDM 1.2 driver.
        BDD_LOG_INFORMATION("DxgkCbAcquirePostDisplayOwnership failed with status 0x%x", Status);
        m_Flags.HasPostDisplay = FALSE;
    }

    Status = EnumerateVBE(m_Flags.HasPostDisplay ? &m_CurrentModes[0].DispInfo : NULL);
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("EnumerateVBE failed with status 0x%x", Status);
        goto OutStopHardware;
    }

    m_Flags.DriverStarted = TRUE;
    *pNumberOfViews = MAX_VIEWS;
    *pNumberOfChildren = MAX_CHILDREN;

    return STATUS_SUCCESS;

OutStopHardware:
    StopHardware();

    return Status;
}

NTSTATUS BASIC_DISPLAY_DRIVER::StopDevice(VOID) {
    PAGED_CODE();

    CleanUp();

    StopHardware();

    m_Flags.DriverStarted = FALSE;

    return STATUS_SUCCESS;
}

VOID BASIC_DISPLAY_DRIVER::CleanUp() {
    PAGED_CODE();

    for (UINT Source = 0; Source < MAX_VIEWS; ++Source) {
        if (m_CurrentModes[Source].FrameBuffer.Ptr) {
            UnmapFrameBuffer(
                m_CurrentModes[Source].FrameBuffer.Ptr,
                m_CurrentModes[Source].DispInfo.Height * m_CurrentModes[Source].DispInfo.Pitch);
            m_CurrentModes[Source].FrameBuffer.Ptr = NULL;
            m_CurrentModes[Source].Flags.FrameBufferIsActive = FALSE;
        }
    }
}

NTSTATUS
BASIC_DISPLAY_DRIVER::DispatchIoRequest(_In_ ULONG VidPnSourceId, _In_ VIDEO_REQUEST_PACKET *pVideoRequestPacket) {
    PAGED_CODE();

    BDD_ASSERT(pVideoRequestPacket != NULL);
    BDD_ASSERT(VidPnSourceId < MAX_VIEWS);

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS BASIC_DISPLAY_DRIVER::SetPowerState(
    _In_ ULONG HardwareUid,
    _In_ DEVICE_POWER_STATE DevicePowerState,
    _In_ POWER_ACTION ActionType) {
    PAGED_CODE();

    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ActionType);

    BDD_ASSERT((HardwareUid < MAX_CHILDREN) || (HardwareUid == DISPLAY_ADAPTER_HW_ID));

    if (HardwareUid == DISPLAY_ADAPTER_HW_ID) {
        if (DevicePowerState == PowerDeviceD0) {
            // get the previous firmware mode
            Status = m_DxgkInterface.DxgkCbAcquirePostDisplayOwnership(
                m_DxgkInterface.DeviceHandle,
                &m_CurrentModes[0].DispInfo);
            if (!NT_SUCCESS(Status) || m_CurrentModes[0].DispInfo.Width == 0) {
                return STATUS_UNSUCCESSFUL;
            }
            if (!NT_SUCCESS(EnumerateVBE(&m_CurrentModes[0].DispInfo))) {
                return STATUS_UNSUCCESSFUL;
            }

            // When returning from D3 the device visibility defined to be off for all targets
            if (m_AdapterPowerState == PowerDeviceD3) {
                DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
                Visibility.VidPnSourceId = D3DDDI_ID_ALL;
                Visibility.Visible = FALSE;
                SetVidPnSourceVisibility(&Visibility);
            }
        }

        m_AdapterPowerState = DevicePowerState;
        return STATUS_SUCCESS;
    } else {
        if (DevicePowerState == PowerDeviceD0) {
            if (m_MonitorPowerState != PowerDeviceD0) {
                DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;
                Visibility.VidPnSourceId = FindSourceForTarget(HardwareUid, TRUE);
                Visibility.Visible = TRUE;
                SetVidPnSourceVisibility(&Visibility);
            }
        }

        m_MonitorPowerState = DevicePowerState;
        return STATUS_SUCCESS;
    }
}

NTSTATUS BASIC_DISPLAY_DRIVER::QueryChildRelations(
    _Out_writes_bytes_(ChildRelationsSize) DXGK_CHILD_DESCRIPTOR *pChildRelations,
    _In_ ULONG ChildRelationsSize) {
    PAGED_CODE();

    BDD_ASSERT(pChildRelations != NULL);

    // The last DXGK_CHILD_DESCRIPTOR in the array of pChildRelations must remain zeroed out, so we subtract this from
    // the count
    ULONG ChildRelationsCount = (ChildRelationsSize / sizeof(DXGK_CHILD_DESCRIPTOR)) - 1;
    BDD_ASSERT(ChildRelationsCount <= MAX_CHILDREN);

    for (UINT ChildIndex = 0; ChildIndex < ChildRelationsCount; ++ChildIndex) {
        pChildRelations[ChildIndex].ChildDeviceType = TypeVideoOutput;
        pChildRelations[ChildIndex].ChildCapabilities.HpdAwareness = HpdAwarenessInterruptible;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.InterfaceTechnology =
            m_CurrentModes[0].Flags.IsInternal ? D3DKMDT_VOT_INTERNAL : D3DKMDT_VOT_OTHER;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.MonitorOrientationAwareness = D3DKMDT_MOA_NONE;
        pChildRelations[ChildIndex].ChildCapabilities.Type.VideoOutput.SupportsSdtvModes = FALSE;
        // TODO: Replace 0 with the actual ACPI ID of the child device, if available
        pChildRelations[ChildIndex].AcpiUid = 0;
        pChildRelations[ChildIndex].ChildUid = ChildIndex;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
BASIC_DISPLAY_DRIVER::QueryChildStatus(_Inout_ DXGK_CHILD_STATUS *pChildStatus, _In_ BOOLEAN NonDestructiveOnly) {
    PAGED_CODE();

    UNREFERENCED_PARAMETER(NonDestructiveOnly);
    BDD_ASSERT(pChildStatus != NULL);
    BDD_ASSERT(pChildStatus->ChildUid < MAX_CHILDREN);

    switch (pChildStatus->Type) {
    case StatusConnection: {
        // HpdAwarenessInterruptible was reported since HpdAwarenessNone is deprecated.
        // However, BDD has no knowledge of HotPlug events, so just always return connected.
        pChildStatus->HotPlug.Connected = IsDriverActive();
        return STATUS_SUCCESS;
    }

    case StatusRotation: {
        // D3DKMDT_MOA_NONE was reported, so this should never be called
        BDD_LOG_ERROR("Child status being queried for StatusRotation even though D3DKMDT_MOA_NONE was reported");
        return STATUS_INVALID_PARAMETER;
    }

    default: {
        BDD_LOG_WARNING("Unknown pChildStatus->Type (0x%x) requested.", pChildStatus->Type);
        return STATUS_NOT_SUPPORTED;
    }
    }
}

// EDID retrieval
NTSTATUS
BASIC_DISPLAY_DRIVER::QueryDeviceDescriptor(_In_ ULONG ChildUid, _Inout_ DXGK_DEVICE_DESCRIPTOR *pDeviceDescriptor) {
    PAGED_CODE();

    BDD_ASSERT(pDeviceDescriptor != NULL);
    BDD_ASSERT(ChildUid < MAX_CHILDREN);

    // If we haven't successfully retrieved an EDID yet (invalid ones are ok, so long as it was retrieved)
    if (!m_Flags.EDID_Attempted) {
        GetEdid(ChildUid);
    }

    if (!m_Flags.EDID_Retrieved || !m_Flags.EDID_ValidHeader || !m_Flags.EDID_ValidChecksum) {
        // Report no EDID if a valid one wasn't retrieved
        return STATUS_GRAPHICS_CHILD_DESCRIPTOR_NOT_SUPPORTED;
    } else if (pDeviceDescriptor->DescriptorOffset == 0) {
        // Only the base block is supported
        RtlCopyMemory(
            pDeviceDescriptor->DescriptorBuffer,
            m_EDIDs[ChildUid],
            min(pDeviceDescriptor->DescriptorLength, EDID_V1_BLOCK_SIZE));

        return STATUS_SUCCESS;
    } else {
        return STATUS_MONITOR_NO_MORE_DESCRIPTOR_DATA;
    }
}

NTSTATUS BASIC_DISPLAY_DRIVER::QueryAdapterInfo(_In_ CONST DXGKARG_QUERYADAPTERINFO *pQueryAdapterInfo) {
    PAGED_CODE();

    BDD_ASSERT(pQueryAdapterInfo != NULL);

    switch (pQueryAdapterInfo->Type) {
    case DXGKQAITYPE_DRIVERCAPS: {
        if (pQueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS)) {
            BDD_LOG_ERROR(
                "pQueryAdapterInfo->OutputDataSize (0x%x) is smaller than sizeof(DXGK_DRIVERCAPS) (0x%zx)",
                pQueryAdapterInfo->OutputDataSize,
                sizeof(DXGK_DRIVERCAPS));
            return STATUS_BUFFER_TOO_SMALL;
        }

        DXGK_DRIVERCAPS *pDriverCaps = (DXGK_DRIVERCAPS *)pQueryAdapterInfo->pOutputData;

        // Nearly all fields must be initialized to zero, so zero out to start and then change those that are non-zero.
        // Fields are zero since BDD is Display-Only and therefore does not support any of the render related fields.
        // It also doesn't support hardware interrupts, gamma ramps, etc.
        RtlZeroMemory(pDriverCaps, sizeof(DXGK_DRIVERCAPS));

        pDriverCaps->WDDMVersion = DXGKDDI_WDDMv1_2;
        pDriverCaps->HighestAcceptableAddress.QuadPart = -1;

        pDriverCaps->SupportNonVGA = TRUE;
        pDriverCaps->SupportSmoothRotation = TRUE;

        return STATUS_SUCCESS;
    }

    case DXGKQAITYPE_DISPLAY_DRIVERCAPS_EXTENSION: {
        DXGK_DISPLAY_DRIVERCAPS_EXTENSION *pDriverDisplayCaps;

        if (pQueryAdapterInfo->OutputDataSize < sizeof(*pDriverDisplayCaps)) {
            BDD_LOG_ERROR(
                "pQueryAdapterInfo->OutputDataSize (0x%x) is smaller than sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION) "
                "(0x%zx)",
                pQueryAdapterInfo->OutputDataSize,
                sizeof(DXGK_DISPLAY_DRIVERCAPS_EXTENSION));

            return STATUS_INVALID_PARAMETER;
        }

        pDriverDisplayCaps = (DXGK_DISPLAY_DRIVERCAPS_EXTENSION *)pQueryAdapterInfo->pOutputData;

        // Reset all caps values
        RtlZeroMemory(pDriverDisplayCaps, pQueryAdapterInfo->OutputDataSize);

        // We claim to support virtual display mode.
        pDriverDisplayCaps->VirtualModeSupport = 1;

        return STATUS_SUCCESS;
    }

    default: {
        // BDD does not need to support any other adapter information types
        BDD_LOG_WARNING("Unknown QueryAdapterInfo Type (0x%x) requested", pQueryAdapterInfo->Type);
        return STATUS_NOT_SUPPORTED;
    }
    }
}

// Even though Sample Basic Display Driver does not support hardware cursors, and reports such
// in QueryAdapterInfo. This function can still be called to set the pointer to not visible
NTSTATUS BASIC_DISPLAY_DRIVER::SetPointerPosition(_In_ CONST DXGKARG_SETPOINTERPOSITION *pSetPointerPosition) {
    PAGED_CODE();

    BDD_ASSERT(pSetPointerPosition != NULL);
    BDD_ASSERT(pSetPointerPosition->VidPnSourceId < MAX_VIEWS);

    if (!(pSetPointerPosition->Flags.Visible)) {
        return STATUS_SUCCESS;
    } else {
        BDD_LOG_ASSERTION(
            "SetPointerPosition should never be called to set the pointer to visible since BDD doesn't support "
            "hardware cursors.");
        return STATUS_UNSUCCESSFUL;
    }
}

// Basic Sample Display Driver does not support hardware cursors, and reports such
// in QueryAdapterInfo. Therefore this function should never be called.
NTSTATUS BASIC_DISPLAY_DRIVER::SetPointerShape(_In_ CONST DXGKARG_SETPOINTERSHAPE *pSetPointerShape) {
    PAGED_CODE();

    BDD_ASSERT(pSetPointerShape != NULL);
    BDD_LOG_ASSERTION("SetPointerShape should never be called since BDD doesn't support hardware cursors.");

    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS BASIC_DISPLAY_DRIVER::PresentDisplayOnly(_In_ CONST DXGKARG_PRESENT_DISPLAYONLY *pPresentDisplayOnly) {
    PAGED_CODE();

    BDD_ASSERT(pPresentDisplayOnly != NULL);
    BDD_ASSERT(pPresentDisplayOnly->VidPnSourceId < MAX_VIEWS);

    if (pPresentDisplayOnly->BytesPerPixel < MIN_BYTES_PER_PIXEL_REPORTED ||
        pPresentDisplayOnly->BytesPerPixel > MAX_BYTES_PER_PIXEL_REPORTED) {
        // Only >=32bpp modes are reported, therefore this Present should never pass anything less than 4 bytes per
        // pixel
        BDD_LOG_ERROR(
            "pPresentDisplayOnly->BytesPerPixel is 0x%lx, which is lower than the allowed.",
            pPresentDisplayOnly->BytesPerPixel);
        return STATUS_INVALID_PARAMETER;
    }

    // If it is in monitor off state or source is not supposed to be visible, don't present anything to the screen
    if ((m_MonitorPowerState > PowerDeviceD0) ||
        (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Flags.SourceNotVisible)) {
        return STATUS_SUCCESS;
    }

    // Present is only valid if the target is actively connected to this source
    if (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Flags.FrameBufferIsActive) {

        // If actual pixels are coming through, will need to completely zero out physical address next time in
        // BlackOutScreen
        m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].ZeroedOutStart.QuadPart = 0;
        m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].ZeroedOutEnd.QuadPart = 0;

        D3DKMDT_VIDPN_PRESENT_PATH_ROTATION RotationNeededByFb = pPresentDisplayOnly->Flags.Rotate
            ? m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Rotation
            : D3DKMDT_VPPR_IDENTITY;
        BYTE *pDst = (BYTE *)m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].FrameBuffer.Ptr;
        UINT DstBitPerPixel =
            BPPFromPixelFormat(m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.ColorFormat);
        if (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].Scaling == D3DKMDT_VPPS_CENTERED) {
            UINT CenterShift = (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Height -
                                m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].SrcModeHeight) *
                m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Pitch;
            CenterShift += (m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].DispInfo.Width -
                            m_CurrentModes[pPresentDisplayOnly->VidPnSourceId].SrcModeWidth) *
                DstBitPerPixel / BITS_PER_BYTE;
            pDst += (int)CenterShift / 2;
        }
        return m_HardwareBlt[pPresentDisplayOnly->VidPnSourceId].ExecutePresentDisplayOnly(
            pDst,
            DstBitPerPixel,
            (BYTE *)pPresentDisplayOnly->pSource,
            pPresentDisplayOnly->BytesPerPixel,
            pPresentDisplayOnly->Pitch,
            pPresentDisplayOnly->NumMoves,
            pPresentDisplayOnly->pMoves,
            pPresentDisplayOnly->NumDirtyRects,
            pPresentDisplayOnly->pDirtyRect,
            RotationNeededByFb);
    }

    return STATUS_SUCCESS;
}

NTSTATUS BASIC_DISPLAY_DRIVER::StopDeviceAndReleasePostDisplayOwnership(
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _Out_ DXGK_DISPLAY_INFORMATION *pDisplayInfo) {
    PAGED_CODE();

    BDD_ASSERT(TargetId < MAX_CHILDREN);

    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId = FindSourceForTarget(TargetId, TRUE);

    // In case BDD is the next driver to run, the monitor should not be off, since
    // this could cause the BIOS to hang when the EDID is retrieved on Start.
    if (m_MonitorPowerState > PowerDeviceD0) {
        SetPowerState(TargetId, PowerDeviceD0, PowerActionNone);
    }

    // The driver has to black out the display and ensure it is visible when releasing ownership
    BlackOutScreen(SourceId);

    *pDisplayInfo = m_CurrentModes[SourceId].DispInfo;

    return StopDevice();
}

NTSTATUS BASIC_DISPLAY_DRIVER::QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps) {
    PAGED_CODE();

    BDD_ASSERT(pVidPnHWCaps != NULL);
    BDD_ASSERT(pVidPnHWCaps->SourceId < MAX_VIEWS);
    BDD_ASSERT(pVidPnHWCaps->TargetId < MAX_CHILDREN);

    pVidPnHWCaps->VidPnHWCaps.DriverRotation = 1;             // BDD does rotation in software
    pVidPnHWCaps->VidPnHWCaps.DriverScaling = 0;              // BDD does not support scaling
    pVidPnHWCaps->VidPnHWCaps.DriverCloning = 0;              // BDD does not support clone
    pVidPnHWCaps->VidPnHWCaps.DriverColorConvert = 1;         // BDD does color conversions in software
    pVidPnHWCaps->VidPnHWCaps.DriverLinkedAdapaterOutput = 0; // BDD does not support linked adapters
    pVidPnHWCaps->VidPnHWCaps.DriverRemoteDisplay = 0;        // BDD does not support remote displays

    return STATUS_SUCCESS;
}

VOID BASIC_DISPLAY_DRIVER::BlackOutScreen(D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId) {
    PAGED_CODE();

    UINT ScreenHeight = m_CurrentModes[SourceId].DispInfo.Height;
    UINT ScreenPitch = m_CurrentModes[SourceId].DispInfo.Pitch;

    PHYSICAL_ADDRESS NewPhysAddrStart = m_CurrentModes[SourceId].DispInfo.PhysicAddress;
    PHYSICAL_ADDRESS NewPhysAddrEnd;
    NewPhysAddrEnd.QuadPart = NewPhysAddrStart.QuadPart + (ScreenHeight * ScreenPitch);

    if (m_CurrentModes[SourceId].Flags.FrameBufferIsActive) {
        BYTE *MappedAddr = reinterpret_cast<BYTE *>(m_CurrentModes[SourceId].FrameBuffer.Ptr);

        // Zero any memory at the start that hasn't been zeroed recently
        if (NewPhysAddrStart.QuadPart < m_CurrentModes[SourceId].ZeroedOutStart.QuadPart) {
            if (NewPhysAddrEnd.QuadPart < m_CurrentModes[SourceId].ZeroedOutStart.QuadPart) {
                // No overlap
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            } else {
                RtlZeroMemory(
                    MappedAddr,
                    (UINT)(m_CurrentModes[SourceId].ZeroedOutStart.QuadPart - NewPhysAddrStart.QuadPart));
            }
        }

        // Zero any memory at the end that hasn't been zeroed recently
        if (NewPhysAddrEnd.QuadPart > m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart) {
            if (NewPhysAddrStart.QuadPart > m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart) {
                // No overlap
                // NOTE: When actual pixels were the most recent thing drawn, ZeroedOutStart & ZeroedOutEnd will both be
                // 0 and this is the path that will be used to black out the current screen.
                RtlZeroMemory(MappedAddr, ScreenHeight * ScreenPitch);
            } else {
                RtlZeroMemory(
                    MappedAddr,
                    (UINT)(NewPhysAddrEnd.QuadPart - m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart));
            }
        }
    }

    m_CurrentModes[SourceId].ZeroedOutStart.QuadPart = NewPhysAddrStart.QuadPart;
    m_CurrentModes[SourceId].ZeroedOutEnd.QuadPart = NewPhysAddrEnd.QuadPart;
}

UINT BASIC_DISPLAY_DRIVER::FindMatchingVBEMode(CONST D3DKMDT_VIDPN_SOURCE_MODE *pSourceMode) const {
    PAGED_CODE();

    for (UINT i = 0; i < m_ModeInfo.Count; i++) {
        if (m_ModeInfo.Modes[i].Width == pSourceMode->Format.Graphics.PrimSurfSize.cx &&
            m_ModeInfo.Modes[i].Height == pSourceMode->Format.Graphics.PrimSurfSize.cy &&
            m_ModeInfo.Modes[i].BitsPerPixel == BPPFromPixelFormat(pSourceMode->Format.Graphics.PixelFormat)) {
            return i;
        }
    }
    return m_ModeInfo.Count;
}

NTSTATUS
BASIC_DISPLAY_DRIVER::WriteHWInfoStr(_In_ HANDLE DevInstRegKeyHandle, _In_ PCWSTR pszwValueName, _In_ PCSTR pszValue) {
    PAGED_CODE();

    NTSTATUS Status;
    ANSI_STRING AnsiStrValue;
    UNICODE_STRING UnicodeStrValue;
    UNICODE_STRING UnicodeStrValueName;

    // ZwSetValueKey wants the ValueName as a UNICODE_STRING
    RtlInitUnicodeString(&UnicodeStrValueName, pszwValueName);

    // REG_SZ is for WCHARs, there is no equivalent for CHARs
    // Use the ansi/unicode conversion functions to get from PSTR to PWSTR
    RtlInitAnsiString(&AnsiStrValue, pszValue);
    Status = RtlAnsiStringToUnicodeString(&UnicodeStrValue, &AnsiStrValue, TRUE);
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("RtlAnsiStringToUnicodeString failed with Status: 0x%x", Status);
        return Status;
    }

    // Write the value to the registry
    Status = ZwSetValueKey(
        DevInstRegKeyHandle,
        &UnicodeStrValueName,
        0,
        REG_SZ,
        UnicodeStrValue.Buffer,
        UnicodeStrValue.MaximumLength);

    // Free the earlier allocated unicode string
    RtlFreeUnicodeString(&UnicodeStrValue);

    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("ZwSetValueKey failed with Status: 0x%x", Status);
    }

    return Status;
}

NTSTATUS BASIC_DISPLAY_DRIVER::RegisterHWInfo() {
    PAGED_CODE();

    NTSTATUS Status;

    PCSTR StrHWInfoChipType = "QEMU/Bochs Standard VGA";
    PCSTR StrHWInfoDacType = "QEMU/Bochs Standard VGA";
    PCSTR StrHWInfoAdapterString = "XCP-ng Standard VGA Display Adapter";
    PCSTR StrHWInfoBiosString = "QEMU/Bochs Standard VGA";

    HANDLE DevInstRegKeyHandle;
    Status = IoOpenDeviceRegistryKey(m_pPhysicalDevice, PLUGPLAY_REGKEY_DRIVER, KEY_SET_VALUE, &DevInstRegKeyHandle);
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("IoOpenDeviceRegistryKey failed for PDO: 0x%p, Status: 0x%x", m_pPhysicalDevice, Status);
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.ChipType", StrHWInfoChipType);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.DacType", StrHWInfoDacType);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.AdapterString", StrHWInfoAdapterString);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    Status = WriteHWInfoStr(DevInstRegKeyHandle, L"HardwareInformation.BiosString", StrHWInfoBiosString);
    if (!NT_SUCCESS(Status)) {
        return Status;
    }

    // MemorySize is a ULONG, unlike the others which are all strings
    UNICODE_STRING ValueNameMemorySize;
    RtlInitUnicodeString(&ValueNameMemorySize, L"HardwareInformation.MemorySize");
    DWORD MemorySize = 0; // BDD has no access to video memory
    Status = ZwSetValueKey(DevInstRegKeyHandle, &ValueNameMemorySize, 0, REG_DWORD, &MemorySize, sizeof(MemorySize));
    if (!NT_SUCCESS(Status)) {
        BDD_LOG_ERROR("ZwSetValueKey for MemorySize failed with Status: 0x%x", Status);
        return Status;
    }

    return Status;
}

//
// Non-Paged Code
//
#pragma code_seg(push)
#pragma code_seg()
D3DDDI_VIDEO_PRESENT_SOURCE_ID
BASIC_DISPLAY_DRIVER::FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero) {
    UNREFERENCED_PARAMETER(TargetId);
    BDD_ASSERT_CHK(TargetId < MAX_CHILDREN);

    for (UINT SourceId = 0; SourceId < MAX_VIEWS; ++SourceId) {
        if (m_CurrentModes[SourceId].FrameBuffer.Ptr != NULL) {
            return SourceId;
        }
    }

    return DefaultToZero ? 0 : D3DDDI_ID_UNINITIALIZED;
}

VOID BASIC_DISPLAY_DRIVER::DpcRoutine(VOID) {
    m_DxgkInterface.DxgkCbNotifyDpc((HANDLE)m_DxgkInterface.DeviceHandle);
}

BOOLEAN BASIC_DISPLAY_DRIVER::InterruptRoutine(_In_ ULONG MessageNumber) {
    UNREFERENCED_PARAMETER(MessageNumber);

    // BDD cannot handle interrupts
    return FALSE;
}

VOID BASIC_DISPLAY_DRIVER::ResetDevice(VOID) {}

// Must be Non-Paged, as it sets up the display for a bugcheck
NTSTATUS BASIC_DISPLAY_DRIVER::SystemDisplayEnable(
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
    _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
    _Out_ UINT *pWidth,
    _Out_ UINT *pHeight,
    _Out_ D3DDDIFORMAT *pColorFormat) {
    UNREFERENCED_PARAMETER(Flags);

    m_SystemDisplaySourceId = D3DDDI_ID_UNINITIALIZED;

    BDD_ASSERT((TargetId < MAX_CHILDREN) || (TargetId == D3DDDI_ID_UNINITIALIZED));

    // Find the frame buffer for displaying the bugcheck, if it was successfully mapped
    if (TargetId == D3DDDI_ID_UNINITIALIZED) {
        for (UINT SourceIdx = 0; SourceIdx < MAX_VIEWS; ++SourceIdx) {
            if (m_CurrentModes[SourceIdx].FrameBuffer.Ptr != NULL) {
                m_SystemDisplaySourceId = SourceIdx;
                break;
            }
        }
    } else {
        m_SystemDisplaySourceId = FindSourceForTarget(TargetId, FALSE);
    }

    if (m_SystemDisplaySourceId == D3DDDI_ID_UNINITIALIZED) {
        {
            return STATUS_UNSUCCESSFUL;
        }
    }

    if ((m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE90) ||
        (m_CurrentModes[m_SystemDisplaySourceId].Rotation == D3DKMDT_VPPR_ROTATE270)) {
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    } else {
        *pWidth = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
        *pHeight = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;
    }

    *pColorFormat = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat;

    return STATUS_SUCCESS;
}

// Must be Non-Paged, as it is called to display the bugcheck screen
VOID BASIC_DISPLAY_DRIVER::SystemDisplayWrite(
    _In_reads_bytes_(SourceHeight *SourceStride) VOID *pSource,
    _In_ UINT SourceWidth,
    _In_ UINT SourceHeight,
    _In_ UINT SourceStride,
    _In_ INT PositionX,
    _In_ INT PositionY) {

    // Rect will be Offset by PositionX/Y in the src to reset it back to 0
    RECT Rect;
    Rect.left = PositionX;
    Rect.top = PositionY;
    Rect.right = Rect.left + SourceWidth;
    Rect.bottom = Rect.top + SourceHeight;

    // Set up destination blt info
    BLT_INFO DstBltInfo{};
    DstBltInfo.pBits = m_CurrentModes[m_SystemDisplaySourceId].FrameBuffer.Ptr;
    DstBltInfo.Pitch = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Pitch;
    DstBltInfo.BitsPerPel = BPPFromPixelFormat(m_CurrentModes[m_SystemDisplaySourceId].DispInfo.ColorFormat);
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = m_CurrentModes[m_SystemDisplaySourceId].Rotation;
    DstBltInfo.Width = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Width;
    DstBltInfo.Height = m_CurrentModes[m_SystemDisplaySourceId].DispInfo.Height;

    // Set up source blt info
    BLT_INFO SrcBltInfo{};
    SrcBltInfo.pBits = pSource;
    SrcBltInfo.Pitch = SourceStride;
    SrcBltInfo.BitsPerPel = 32;

    SrcBltInfo.Offset.x = -PositionX;
    SrcBltInfo.Offset.y = -PositionY;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    SrcBltInfo.Width = SourceWidth;
    SrcBltInfo.Height = SourceHeight;

    BltBits(
        &DstBltInfo,
        &SrcBltInfo,
        1, // NumRects
        &Rect);
}

#pragma code_seg(pop) // End Non-Paged Code
