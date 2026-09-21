using System;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace EzReleaseTests
{
    // A job owns descendants even after the root exits. Launch suspended so no child can escape
    // before assignment. Do not request breakaway: incompatible inherited jobs fail closed.
    public sealed class OwnedProcess : IDisposable
    {
        private SafeFileHandle job;
        private StreamReader stdout, stderr;
        public Process Process { get; private set; }
        public Task<string> StdOut { get; private set; }
        public Task<string> StdErr { get; private set; }
        public uint ActiveProcessCount
        {
            get
            {
                if (job == null || job.IsClosed) return 0;
                BASIC_ACCOUNTING_INFORMATION accounting;
                if (!QueryInformationJobObject(job, 1, out accounting, Marshal.SizeOf(typeof(BASIC_ACCOUNTING_INFORMATION)), IntPtr.Zero))
                    throw Error("QueryInformationJobObject");
                return accounting.ActiveProcesses;
            }
        }

        public static OwnedProcess Start(string executable, string arguments, string directory)
        {
            var owner = new OwnedProcess();
            IntPtr outRead = IntPtr.Zero, outWrite = IntPtr.Zero;
            IntPtr errRead = IntPtr.Zero, errWrite = IntPtr.Zero, input = IntPtr.Zero;
            IntPtr attributes = IntPtr.Zero, handles = IntPtr.Zero;
            bool attributesInitialized = false;
            bool launched = false;
            PROCESS_INFORMATION pi = new PROCESS_INFORMATION();
            try
            {
                owner.job = new SafeFileHandle(CreateJobObject(IntPtr.Zero, null), true);
                if (owner.job.IsInvalid) throw Error("CreateJobObject");
                var limits = new EXTENDED_LIMIT_INFORMATION();
                limits.Basic.LimitFlags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
                if (!SetInformationJobObject(owner.job, 9, ref limits, Marshal.SizeOf(limits)))
                    throw Error("SetInformationJobObject");

                var security = new SECURITY_ATTRIBUTES();
                security.Length = Marshal.SizeOf(security);
                security.InheritHandle = true;
                if (!CreatePipe(out outRead, out outWrite, ref security, 0)) throw Error("CreatePipe(stdout)");
                if (!CreatePipe(out errRead, out errWrite, ref security, 0)) throw Error("CreatePipe(stderr)");
                if (!SetHandleInformation(outRead, 1, 0) || !SetHandleInformation(errRead, 1, 0))
                    throw Error("SetHandleInformation");
                input = CreateFile("NUL", 0x80000000, 3, ref security, 3, 0, IntPtr.Zero);
                if (input == new IntPtr(-1)) throw Error("CreateFile(NUL)");

                // Inherit only these three handles, not other live launches' pipes or job handles.
                IntPtr size = IntPtr.Zero;
                InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref size);
                attributes = Marshal.AllocHGlobal(size);
                if (!InitializeProcThreadAttributeList(attributes, 1, 0, ref size)) throw Error("InitializeProcThreadAttributeList");
                attributesInitialized = true;
                handles = Marshal.AllocHGlobal(IntPtr.Size * 3);
                Marshal.Copy(new[] { input, outWrite, errWrite }, 0, handles, 3);
                if (!UpdateProcThreadAttribute(attributes, 0, new IntPtr(0x20002), handles,
                    new IntPtr(IntPtr.Size * 3), IntPtr.Zero, IntPtr.Zero)) throw Error("UpdateProcThreadAttribute(handle list)");
                var startup = new STARTUPINFOEX();
                startup.Startup.Size = Marshal.SizeOf(startup);
                startup.Startup.Flags = 0x100; // STARTF_USESTDHANDLES
                startup.Startup.Input = input;
                startup.Startup.Output = outWrite;
                startup.Startup.Error = errWrite;
                startup.Attributes = attributes;
                var command = new StringBuilder("\"" + executable + "\" " + arguments);
                // CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT
                if (!CreateProcess(executable, command, IntPtr.Zero, IntPtr.Zero, true, 0x08080004,
                    IntPtr.Zero, String.IsNullOrEmpty(directory) ? null : directory, ref startup, out pi))
                    throw Error("CreateProcess");
                if (!AssignProcessToJobObject(owner.job, pi.Process)) throw Error("AssignProcessToJobObject");
                owner.Process = Process.GetProcessById(pi.ProcessId);
                // Pin the real process handle before resuming; PS 5.1 must not reopen a dead PID
                // when retrieving ExitCode (nor accidentally observe a reused process ID).
                IntPtr retainedHandle = owner.Process.Handle;
                owner.stdout = new StreamReader(new FileStream(new SafeFileHandle(outRead, true), FileAccess.Read));
                outRead = IntPtr.Zero;
                owner.stderr = new StreamReader(new FileStream(new SafeFileHandle(errRead, true), FileAccess.Read));
                errRead = IntPtr.Zero;
                owner.StdOut = owner.stdout.ReadToEndAsync();
                owner.StdErr = owner.stderr.ReadToEndAsync();
                if (ResumeThread(pi.Thread) == UInt32.MaxValue) throw Error("ResumeThread");
                launched = true;
                return owner;
            }
            catch (Exception failure)
            {
                // A failed assignment still leaves a suspended process outside the job. Kill it by
                // its original handle, never by a PID lookup, before releasing launch ownership.
                if (pi.Process != IntPtr.Zero)
                {
                    if (!TerminateProcess(pi.Process, 1))
                        throw new AggregateException(failure, Error("Failed launch cleanup"));
                    uint wait = WaitForSingleObject(pi.Process, 10000);
                    if (wait == 258) throw new AggregateException(failure, new TimeoutException("Failed launch did not terminate within 10 seconds."));
                    if (wait != 0) throw new AggregateException(failure, Error("WaitForSingleObject(failed launch)"));
                }
                throw;
            }
            finally
            {
                Close(ref outWrite); Close(ref errWrite); Close(ref input);
                Close(ref outRead); Close(ref errRead);
                Close(ref pi.Thread); Close(ref pi.Process);
                if (attributesInitialized) DeleteProcThreadAttributeList(attributes);
                if (attributes != IntPtr.Zero) Marshal.FreeHGlobal(attributes);
                if (handles != IntPtr.Zero) Marshal.FreeHGlobal(handles);
                // Success transfers the job to the caller. Failed launches never resume user code
                // before assignment; SafeHandle also supplies kill-on-close on host termination.
                if (!launched)
                {
                    if (owner.job != null) owner.job.Dispose();
                    if (owner.stdout != null) owner.stdout.Dispose();
                    if (owner.stderr != null) owner.stderr.Dispose();
                    if (owner.Process != null) owner.Process.Dispose();
                }
            }
        }

        public void Stop()
        {
            // Keep the root Process usable for exit-code/timing queries until Dispose. The job
            // and pipe handles are released here; detached callers dispose after saving evidence.
            if (job == null || job.IsClosed) return;
            if (!TerminateJobObject(job, 1)) throw Error("TerminateJobObject");
            var timer = Stopwatch.StartNew();
            for (;;)
            {
                if (ActiveProcessCount == 0) break;
                if (timer.ElapsedMilliseconds >= 10000) throw new TimeoutException("Owned process job did not terminate within 10 seconds.");
                Thread.Sleep(10);
            }
            if (!Process.WaitForExit(10000)) throw new TimeoutException("Owned root did not signal exit.");
            job.Dispose();
            // All writers are now gone. Bounded drain preserves output without waiting forever
            // on an inherited pipe; failures are diagnostics, never a successful cleanup report.
            if (!Task.WaitAll(new Task[] { StdOut, StdErr }, 10000))
                throw new TimeoutException("Owned process output did not close within 10 seconds.");
            stdout.Dispose(); stderr.Dispose();
        }

        public void Dispose()
        {
            try { Stop(); }
            finally
            {
                if (job != null) job.Dispose();
                if (Process != null) Process.Dispose();
            }
        }

        private static Exception Error(string operation)
        {
            int code = Marshal.GetLastWin32Error();
            return new Win32Exception(code, operation + " failed (Win32 " + code + "): " + new Win32Exception(code).Message);
        }
        private static void Close(ref IntPtr handle)
        {
            if (handle != IntPtr.Zero && handle != new IntPtr(-1)) CloseHandle(handle);
            handle = IntPtr.Zero;
        }
        [StructLayout(LayoutKind.Sequential)] private struct SECURITY_ATTRIBUTES { public int Length; public IntPtr Descriptor; [MarshalAs(UnmanagedType.Bool)] public bool InheritHandle; }
        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)] private struct STARTUPINFO
        {
            public int Size; public string Reserved, Desktop, Title;
            public int X, Y, XSize, YSize, XChars, YChars, Fill, Flags;
            public short ShowWindow, ReservedSize; public IntPtr ReservedPointer, Input, Output, Error;
        }
        [StructLayout(LayoutKind.Sequential)] private struct STARTUPINFOEX { public STARTUPINFO Startup; public IntPtr Attributes; }
        [StructLayout(LayoutKind.Sequential)] private struct PROCESS_INFORMATION { public IntPtr Process, Thread; public int ProcessId, ThreadId; }
        [StructLayout(LayoutKind.Sequential)] private struct BASIC_LIMIT_INFORMATION
        {
            public long ProcessTime, JobTime; public uint LimitFlags; public UIntPtr MinimumWorkingSet, MaximumWorkingSet;
            public uint ActiveProcessLimit; public UIntPtr Affinity; public uint PriorityClass, SchedulingClass;
        }
        [StructLayout(LayoutKind.Sequential)] private struct IO_COUNTERS { public ulong ReadOperations, WriteOperations, OtherOperations, ReadBytes, WriteBytes, OtherBytes; }
        [StructLayout(LayoutKind.Sequential)] private struct EXTENDED_LIMIT_INFORMATION
        {
            public BASIC_LIMIT_INFORMATION Basic; public IO_COUNTERS IO;
            public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemory, PeakJobMemory;
        }
        [StructLayout(LayoutKind.Sequential)] private struct BASIC_ACCOUNTING_INFORMATION
        {
            public long UserTime, KernelTime, PeriodUserTime, PeriodKernelTime;
            public uint PageFaults, TotalProcesses, ActiveProcesses, TerminatedProcesses;
        }
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr CreateJobObject(IntPtr security, string name);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool SetInformationJobObject(SafeFileHandle job, int kind, ref EXTENDED_LIMIT_INFORMATION info, int size);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool AssignProcessToJobObject(SafeFileHandle job, IntPtr process);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool TerminateJobObject(SafeFileHandle job, uint code);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool QueryInformationJobObject(SafeFileHandle job, int kind, out BASIC_ACCOUNTING_INFORMATION info, int size, IntPtr returned);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool CreatePipe(out IntPtr read, out IntPtr write, ref SECURITY_ATTRIBUTES security, uint size);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool SetHandleInformation(IntPtr handle, uint mask, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern IntPtr CreateFile(string name, uint access, uint sharing, ref SECURITY_ATTRIBUTES security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool InitializeProcThreadAttributeList(IntPtr list, int count, int flags, ref IntPtr size);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool UpdateProcThreadAttribute(IntPtr list, uint flags, IntPtr attribute, IntPtr value, IntPtr size, IntPtr previous, IntPtr returned);
        [DllImport("kernel32.dll")] private static extern void DeleteProcThreadAttributeList(IntPtr list);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] private static extern bool CreateProcess(string application, StringBuilder command, IntPtr processSecurity, IntPtr threadSecurity, bool inherit, uint flags, IntPtr environment, string directory, ref STARTUPINFOEX startup, out PROCESS_INFORMATION process);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool TerminateProcess(IntPtr process, uint code);
        [DllImport("kernel32.dll", SetLastError = true)] private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);
        [DllImport("kernel32.dll")] private static extern bool CloseHandle(IntPtr handle);
    }
}
