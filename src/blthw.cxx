// SPDX-License-Identifier: MS-PL

// Based on the Microsoft KMDOD example
// Copyright (c) 2010 Microsoft Corporation

#include "bdd.hxx"

#pragma code_seg("PAGE")

typedef struct _DO_PRESENT_MEMORY {
    PVOID DstAddr;
    UINT DstStride;
    ULONG DstBitPerPixel;
    UINT SrcWidth;
    UINT SrcHeight;
    BYTE *SrcAddr;
    LONG SrcPitch;
    ULONG NumMoves;          // in:  Number of screen to screen moves
    D3DKMT_MOVE_RECT *Moves; // in:  Point to the list of moves
    ULONG NumDirtyRects;     // in:  Number of direct rects
    RECT *DirtyRect;         // in:  Point to the list of dirty rects
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceID;
    HANDLE hAdapter;
    BDD_HWBLT *DisplaySource;
} DO_PRESENT_MEMORY, *PDO_PRESENT_MEMORY;

static void HwExecutePresentDisplayOnly(PDO_PRESENT_MEMORY Context)
/*++

  Routine Description:

    The routine executes present's commands and report progress to the OS

  Arguments:

    Context - Context with present's command

  Return Value:

    None

--*/
{
    PAGED_CODE();

    // Set up destination blt info
    BLT_INFO DstBltInfo;
    DstBltInfo.pBits = Context->DstAddr;
    DstBltInfo.Pitch = Context->DstStride;
    DstBltInfo.BitsPerPel = Context->DstBitPerPixel;
    DstBltInfo.Offset.x = 0;
    DstBltInfo.Offset.y = 0;
    DstBltInfo.Rotation = Context->Rotation;
    DstBltInfo.Width = Context->SrcWidth;
    DstBltInfo.Height = Context->SrcHeight;

    // Set up source blt info
    BLT_INFO SrcBltInfo;
    SrcBltInfo.pBits = Context->SrcAddr;
    SrcBltInfo.Pitch = Context->SrcPitch;
    SrcBltInfo.BitsPerPel = 32;
    SrcBltInfo.Offset.x = 0;
    SrcBltInfo.Offset.y = 0;
    SrcBltInfo.Rotation = D3DKMDT_VPPR_IDENTITY;
    if (Context->Rotation == D3DKMDT_VPPR_ROTATE90 || Context->Rotation == D3DKMDT_VPPR_ROTATE270) {
        SrcBltInfo.Width = DstBltInfo.Height;
        SrcBltInfo.Height = DstBltInfo.Width;
    } else {
        SrcBltInfo.Width = DstBltInfo.Width;
        SrcBltInfo.Height = DstBltInfo.Height;
    }

    // Copy all the scroll rects from source image to video frame buffer.
    for (UINT i = 0; i < Context->NumMoves; i++) {
        BltBits(
            &DstBltInfo,
            &SrcBltInfo,
            1, // NumRects
            &Context->Moves[i].DestRect);
    }

    // Copy all the dirty rects from source image to video frame buffer.
    for (UINT i = 0; i < Context->NumDirtyRects; i++) {
        BltBits(
            &DstBltInfo,
            &SrcBltInfo,
            1, // NumRects
            &Context->DirtyRect[i]);
    }

    delete[] reinterpret_cast<BYTE *>(Context);
}

BDD_HWBLT::BDD_HWBLT() : m_SourceId(D3DDDI_ID_UNINITIALIZED), m_DevExt(NULL) {
    PAGED_CODE();
}

BDD_HWBLT::~BDD_HWBLT()
/*++

  Routine Description:

    This routine waits on present worker thread to exit before
    destroying the object

  Arguments:

    None

  Return Value:

    None

--*/
{
    PAGED_CODE();
}

NTSTATUS
BDD_HWBLT::ExecutePresentDisplayOnly(
    _In_ BYTE *DstAddr,
    _In_ UINT DstBitPerPixel,
    _In_ BYTE *SrcAddr,
    _In_ UINT SrcBytesPerPixel,
    _In_ LONG SrcPitch,
    _In_ ULONG NumMoves,
    _In_ D3DKMT_MOVE_RECT *Moves,
    _In_ ULONG NumDirtyRects,
    _In_ RECT *DirtyRect,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
/*++

  Routine Description:

    The method creates present worker thread and provides context
    for it filled with present commands

  Arguments:

    DstAddr - address of destination surface
    DstBitPerPixel - color depth of destination surface
    SrcAddr - address of source surface
    SrcBytesPerPixel - bytes per pixel of source surface
    SrcPitch - source surface pitch (bytes in a row)
    NumMoves - number of moves to be copied
    Moves - moves' data
    NumDirtyRects - number of rectangles to be copied
    DirtyRect - rectangles' data
    Rotation - roatation to be performed when executing copy
    CallBack - callback for present worker thread to report execution status

  Return Value:

    Status

--*/
{
    PAGED_CODE();

    UNREFERENCED_PARAMETER(SrcBytesPerPixel);

    SIZE_T sizeMoves = NumMoves * sizeof(D3DKMT_MOVE_RECT);
    SIZE_T sizeRects = NumDirtyRects * sizeof(RECT);
    SIZE_T size = sizeof(DO_PRESENT_MEMORY) + sizeMoves + sizeRects;

    PDO_PRESENT_MEMORY Context = reinterpret_cast<PDO_PRESENT_MEMORY>(new (PagedPool) BYTE[size]);

    if (!Context) {
        return STATUS_NO_MEMORY;
    }

    const CURRENT_BDD_MODE *pModeCur = m_DevExt->GetCurrentMode(m_SourceId);

    Context->DstAddr = DstAddr;
    Context->DstBitPerPixel = DstBitPerPixel;
    Context->DstStride = pModeCur->DispInfo.Pitch;
    Context->SrcWidth = pModeCur->SrcModeWidth;
    Context->SrcHeight = pModeCur->SrcModeHeight;
    Context->SrcAddr = SrcAddr;
    Context->SrcPitch = SrcPitch;
    Context->Rotation = Rotation;
    Context->NumMoves = NumMoves;
    Context->Moves = Moves;
    Context->NumDirtyRects = NumDirtyRects;
    Context->DirtyRect = DirtyRect;
    Context->SourceID = m_SourceId;
    Context->hAdapter = m_DevExt;
    Context->DisplaySource = this;

    BYTE *rects = reinterpret_cast<BYTE *>(Context + 1);

    // copy moves and update pointer
    if (Moves) {
        memcpy(rects, Moves, sizeMoves);
        Context->Moves = reinterpret_cast<D3DKMT_MOVE_RECT *>(rects);
        rects += sizeMoves;
    }

    // copy dirty rects and update pointer
    if (DirtyRect) {
        memcpy(rects, DirtyRect, sizeRects);
        Context->DirtyRect = reinterpret_cast<RECT *>(rects);
    }

    HwExecutePresentDisplayOnly(Context);
    return STATUS_SUCCESS;
}
