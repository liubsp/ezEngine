using System;
using System.ComponentModel;
using System.Reflection;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace EzReleaseTests
{
    // Exercise a real native cleanup failure without revoking the test's ability to clean up.
    // Only the duplicate loses terminate rights; the original job handle stays owned throughout.
    public static class OwnedProcessTestProbe
    {
        public static void VerifyDeniedTermination(object owner)
        {
            var field = owner.GetType().GetField("job", BindingFlags.Instance | BindingFlags.NonPublic);
            var original = (SafeFileHandle)field.GetValue(owner);
            IntPtr duplicate;
            if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), out duplicate,
                4 /* JOB_OBJECT_QUERY */, false, 0)) throw new Win32Exception(Marshal.GetLastWin32Error());
            using (var limited = new SafeFileHandle(duplicate, true))
            {
                try
                {
                    field.SetValue(owner, limited);
                    try { owner.GetType().GetMethod("Stop").Invoke(owner, null); }
                    catch (TargetInvocationException error)
                    {
                        var native = error.InnerException as Win32Exception;
                        if (native != null && native.NativeErrorCode == 5 && native.Message.Contains("TerminateJobObject")) return;
                        throw;
                    }
                    throw new Exception("Denied termination incorrectly reported success.");
                }
                finally { field.SetValue(owner, original); }
            }
        }

        [DllImport("kernel32.dll")] private static extern IntPtr GetCurrentProcess();
        [DllImport("kernel32.dll", SetLastError = true)] private static extern bool DuplicateHandle(
            IntPtr sourceProcess, SafeFileHandle source, IntPtr targetProcess, out IntPtr target,
            uint access, bool inherit, uint options);
    }
}
