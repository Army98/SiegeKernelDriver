#include <ntifs.h>

#define INVALID_POS 1000000u

typedef NTSTATUS(*PMMCOPYVIRTUALMEMORY)(
    PEPROCESS SourceProcess,
    PVOID SourceAddress,
    PEPROCESS TargetProcess,
    PVOID TargetAddress,
    SIZE_T BufferSize,
    KPROCESSOR_MODE PreviousMode,
    PSIZE_T ReturnSize
    );

typedef PVOID(NTAPI* TPsGetProcessSectionBaseAddress)(PEPROCESS Process);
BOOLEAN FloatBitsNearlyEqual(UINT32 a, UINT32 b, UINT32 maxDelta);

PMMCOPYVIRTUALMEMORY g_MmCopyVirtualMemory = NULL;

HANDLE g_ThreadHandle = NULL;
HANDLE g_ImguiPid = (HANDLE)28944;


VOID Routine(PVOID StartContext);
VOID PrintMessage(const char* msg);

NTSTATUS ReadUserMemory(
    _In_  PEPROCESS SourceProcess,
    _In_  const void* SourceAddress,
    _Out_writes_bytes_(Size) void* TargetBuffer,
    _In_  SIZE_T Size
);

NTSTATUS ReadPointerChain(
    PEPROCESS SourceProcess,
    PVOID BaseAddress,
    PVOID* Out,
    ULONG_PTR* Offsets,
    SIZE_T Count
);

NTSTATUS WriteUserMemory(
    PEPROCESS TargetProcess,
    PVOID TargetAddress,
    PVOID SourceBuffer,
    SIZE_T Size
);


NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);

    UNICODE_STRING mmName = RTL_CONSTANT_STRING(L"MmCopyVirtualMemory");
    g_MmCopyVirtualMemory =
        (PMMCOPYVIRTUALMEMORY)MmGetSystemRoutineAddress(&mmName);

    if (!g_MmCopyVirtualMemory)
    {
        DbgPrintEx(
            DPFLTR_DEFAULT_ID,
            DPFLTR_ERROR_LEVEL,
            "[-] MmCopyVirtualMemory not found\n"
        );
        return STATUS_NOT_SUPPORTED;
    }

    NTSTATUS status = PsCreateSystemThread(
        &g_ThreadHandle,
        GENERIC_ALL,
        NULL,
        NULL,
        NULL,
        Routine,
        NULL
    );

    if (!NT_SUCCESS(status))
    {
        DbgPrintEx(
            DPFLTR_DEFAULT_ID,
            DPFLTR_ERROR_LEVEL,
            "[-] Failed to create system thread\n"
        );
        return status;
    }

    DbgPrintEx(
        DPFLTR_DEFAULT_ID,
        DPFLTR_ERROR_LEVEL,
        "[+] Driver loaded\n"
    );

    return STATUS_SUCCESS;
}

VOID Routine(PVOID StartContext)
{
    UNREFERENCED_PARAMETER(StartContext);

    DbgPrintEx(
        DPFLTR_DEFAULT_ID,
        DPFLTR_ERROR_LEVEL,
        "[+] Routine started\n"
    );

    UNICODE_STRING psName =
        RTL_CONSTANT_STRING(L"PsGetProcessSectionBaseAddress");

    TPsGetProcessSectionBaseAddress PsGetProcessSectionBaseAddress =
        (TPsGetProcessSectionBaseAddress)
        MmGetSystemRoutineAddress(&psName);

    if (!PsGetProcessSectionBaseAddress)
    {
        PrintMessage("[-] Failed to resolve PsGetProcessSectionBaseAddress\n");
        PsTerminateSystemThread(STATUS_UNSUCCESSFUL);
        return;
    }

    PEPROCESS ImguiProcess = NULL;
    NTSTATUS status =
        PsLookupProcessByProcessId(g_ImguiPid, &ImguiProcess);

    if (!NT_SUCCESS(status))
    {
        PrintMessage("[-] Invalid ImGui PID\n");
        PsTerminateSystemThread(status);
        return;
    }

    PrintMessage("[+] ImGui process resolved\n");

    LARGE_INTEGER delay;
    delay.QuadPart = -10 * 1000 ; // 1 second

    BOOLEAN haveRainbow = FALSE;
    PEPROCESS RainbowProcess = NULL;

    PVOID siegePidAddress = (PVOID)0x1402BAB40;
    PVOID viewMatrixOffset = (PVOID)0x1402BAB48;
    PVOID positionOffset = (PVOID)0x1402BABAC;
    int siegePid = 0;

    ULONG_PTR offsets[] = { 0x80, 0x198 };
    PVOID basePtr = NULL;
    static PVOID g_LastEntityBase = NULL;

    static UINT32 g_LastEnt90Bits[100] = { 0 };
    static BOOLEAN g_HasEnt90[100] = { FALSE };

    for (;;)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &delay);

        if (PsGetProcessExitStatus(ImguiProcess) != STATUS_PENDING)
            break;

        if (!haveRainbow)
        {
            status = ReadUserMemory(
                ImguiProcess,
                siegePidAddress,
                &siegePid,
                sizeof(siegePid)
            );

            if (!NT_SUCCESS(status) || siegePid <= 0)
                continue;

            status = PsLookupProcessByProcessId(
                ULongToHandle(siegePid),
                &RainbowProcess
            );

            if (!NT_SUCCESS(status))
            {
                DbgPrintEx(
                    DPFLTR_DEFAULT_ID,
                    DPFLTR_ERROR_LEVEL,
                    "FUCK FUCK FUCK\n"
                );
                continue;
            }

            haveRainbow = TRUE;
            continue;
        }

        if (PsGetProcessExitStatus(RainbowProcess) != STATUS_PENDING)
        {
            ObDereferenceObject(RainbowProcess);
            RainbowProcess = NULL;
            haveRainbow = FALSE;
            continue;
        }

        PVOID r6Base =
            PsGetProcessSectionBaseAddress(RainbowProcess);

        if (!r6Base)
            continue;

        PVOID GmBase = NULL;
        PVOID GM = NULL;

        if (!r6Base)
            continue;

        if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)r6Base + 0x151D3478), &GmBase, sizeof(PVOID))) || !GmBase)
            continue;

        if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)GmBase + 0x90), &GM, sizeof(PVOID))) || !GM)
            continue;

        UINT32 ViewMatrix[16] = { 0 };

        if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)GM + 0x1e2B0), ViewMatrix, sizeof(ViewMatrix))))
        {
            continue;
        }

        for (int i = 0; i < 16; i++)
        {
            WriteUserMemory(ImguiProcess, (PVOID)((ULONG_PTR)viewMatrixOffset + i * 4), &ViewMatrix[i], sizeof(UINT32));
        }
       

        status = ReadPointerChain(
            RainbowProcess,
            (PVOID)((ULONG_PTR)r6Base + 0x15017150),// Ent BASE
            &basePtr,
            offsets,
            RTL_NUMBER_OF(offsets)
        );

        if (!NT_SUCCESS(status))
            continue;

        if (basePtr != g_LastEntityBase)
        {
            g_LastEntityBase = basePtr;
            continue;
        }

        for (int i = 0; i < 200; i++)
        {
            PVOID currEnt =
                (PVOID)((ULONG_PTR)basePtr + 0x28 + (i * 0x38));

            PVOID ent;
            if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, currEnt, &ent, sizeof(PVOID))))
                continue;

            if (!ent)
                continue;

            if ((ULONG_PTR)ent < 0x10000)
                continue;

            if ((ULONG_PTR)ent > 0x00007FFFFFFFFFFF)
                continue;

            UCHAR probe;
            if (!NT_SUCCESS(ReadUserMemory(
                RainbowProcess,
                ent,
                &probe,
                sizeof(UCHAR))))
            {
                continue;
            }

            UINT32 px;
            UINT32 py;
            UINT32 pz;

            if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)ent + 0x50), &px, sizeof(UINT32))))
                continue;
            if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)ent + 0x54), &py, sizeof(UINT32))))
                continue;
            if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)ent + 0x58), &pz, sizeof(UINT32))))
                continue;

            UINT64 value;
            INT32 ID;

            if (!NT_SUCCESS(ReadUserMemory(
                RainbowProcess,
                (PVOID)((ULONG_PTR)ent + 0xB0),
                &value,
                sizeof(value))))
            {
                continue;
            }

            if (!NT_SUCCESS(ReadUserMemory(
                RainbowProcess,
                (PVOID)((ULONG_PTR)ent + 0x1C),
                &ID,
                sizeof(ID))))
            {
                continue;
            }

            // classID
            PVOID vtable;
            PVOID classDescriptor;
            INT64 classID;
            PVOID activeCompList;
            PVOID activeCompListObject;
            BOOLEAN found = FALSE;
            if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)ent + 0xB8), &activeCompList, sizeof(activeCompList))))
                continue;
            for (int j = 0; j < 50; j++)
            {
                if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)activeCompList + j * 8), &activeCompListObject, sizeof(activeCompListObject))))
                    continue;

                if (!activeCompListObject)
                    continue;
                if ((ULONG_PTR)activeCompListObject < 0x10000 ||
                    (ULONG_PTR)activeCompListObject > 0x00007FFFFFFFFFFF)
                    continue;

                if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)activeCompListObject + 0), &vtable, sizeof(vtable))))
                    continue;
                if ((ULONG_PTR)vtable < 0x10000 ||
                    (ULONG_PTR)vtable > 0x00007FFFFFFFFFFF)
                    continue;

                if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)vtable + 0x58), &classDescriptor, sizeof(classDescriptor))))
                    continue;
                if ((ULONG_PTR)classDescriptor < 0x10000 ||
                    (ULONG_PTR)classDescriptor > 0x00007FFFFFFFFFFF)
                    continue;

                if (!NT_SUCCESS(ReadUserMemory(RainbowProcess, (PVOID)((ULONG_PTR)classDescriptor + 0x1C), &classID, sizeof(classID))))
                    continue;

                classID -= 0x8FCF0756B0B42803ULL;

                UINT8* b = (UINT8*)&classID;
                b[0] ^= 0x66;
                b[1] ^= 0x9E;
                b[2] ^= 0x50;
                b[3] ^= 0x5B;

                if ((UINT32)classID == 0x6928d2c7)
                {
                    found = TRUE;
                    break;   // stop early
                }
                /* Print XORed value */
                /*DbgPrintEx(
                    DPFLTR_DEFAULT_ID,
                    0,
                    "XORed ClassID: 0x%08X\n",
                    (UINT32)classID
                );
                */
            }

            if (!found)
                continue;   
          
            UINT8 topByte = (value >> 56) & 0xFF;
                
            if (topByte != 0x24)
                continue;

            if (ID < 0 || ID > 1000)
                continue;

            WriteUserMemory(ImguiProcess, (PVOID)((ULONG_PTR)positionOffset + i * 12 + 0), &px, sizeof(UINT32));
            WriteUserMemory(ImguiProcess, (PVOID)((ULONG_PTR)positionOffset + i * 12 + 4), &py, sizeof(UINT32));
            WriteUserMemory(ImguiProcess, (PVOID)((ULONG_PTR)positionOffset + i * 12 + 8), &pz, sizeof(UINT32));
        }
    }

    if (RainbowProcess)
        ObDereferenceObject(RainbowProcess);    

    if (ImguiProcess)
        ObDereferenceObject(ImguiProcess);

    PrintMessage("[+] Routine exiting\n");
    PsTerminateSystemThread(STATUS_SUCCESS);
}

//
// ========================
// Helpers
// ========================
//
VOID PrintMessage(const char* msg)
{
    DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, msg);
}
static __forceinline BOOLEAN IsUserVa(_In_ const void* p)
{
    ULONG_PTR a = (ULONG_PTR)p;
    return (p != NULL) &&
        (a >= 0x10000) &&
        (a <= (ULONG_PTR)MmHighestUserAddress);
}

NTSTATUS ReadUserMemory(
    _In_  PEPROCESS SourceProcess,
    _In_  const void* SourceAddress,
    _Out_writes_bytes_(Size) void* TargetBuffer,
    _In_  SIZE_T Size
)
{
    if (!IsUserVa(SourceAddress) || TargetBuffer == NULL || Size == 0)
        return STATUS_INVALID_PARAMETER;

    SIZE_T copied = 0;

    NTSTATUS st = g_MmCopyVirtualMemory(
        SourceProcess,
        (PVOID)SourceAddress,
        PsGetCurrentProcess(),
        TargetBuffer,
        Size,
        KernelMode,              
        &copied
    );

    if (!NT_SUCCESS(st))
        return st;

    return (copied == Size) ? STATUS_SUCCESS : STATUS_PARTIAL_COPY;
}

/*
* NTSTATUS ReadUserMemory(
    PEPROCESS SourceProcess,
    PVOID SourceAddress,
    PVOID TargetBuffer,
    SIZE_T Size
)
{
    SIZE_T bytes = 0;

    return g_MmCopyVirtualMemory(
        SourceProcess,
        SourceAddress,
        PsGetCurrentProcess(),
        TargetBuffer,
        Size,
        KernelMode,
        &bytes
    );
}
*/
NTSTATUS WriteUserMemory(
    PEPROCESS TargetProcess,
    PVOID TargetAddress,
    PVOID SourceBuffer,
    SIZE_T Size
)
{
    SIZE_T bytes = 0;

    return g_MmCopyVirtualMemory(
        PsGetCurrentProcess(),  
        SourceBuffer,
        TargetProcess,           
        TargetAddress,
        Size,
        KernelMode,
        &bytes
    );
}


NTSTATUS ReadPointerChain(
    PEPROCESS SourceProcess,
    PVOID BaseAddress,
    PVOID* Out,
    ULONG_PTR* Offsets,
    SIZE_T Count
)
{
    PVOID current = NULL;

    if (!NT_SUCCESS(ReadUserMemory(
        SourceProcess,
        BaseAddress,
        &current,
        sizeof(PVOID))) || !current)
        return STATUS_UNSUCCESSFUL;

    for (SIZE_T i = 0; i < Count; i++)
    {
        if (!NT_SUCCESS(ReadUserMemory(
            SourceProcess,
            (PVOID)((ULONG_PTR)current + Offsets[i]),
            &current,
            sizeof(PVOID))) || !current)
            return STATUS_UNSUCCESSFUL;
    }

    *Out = current;
    return STATUS_SUCCESS;
}

__forceinline
BOOLEAN FloatBitsNearlyEqual(UINT32 a, UINT32 b, UINT32 maxDelta)
{
    if (a > b)
        return (a - b) <= maxDelta;
    else
        return (b - a) <= maxDelta;
}
