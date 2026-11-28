using System;
using System.Runtime.InteropServices;

namespace TaskbarMonitor.Sensors
{
    internal static class AmdAdlxTemperature
    {
        private const uint LoadLibrarySearchSystem32 = 0x800;
        // ADLX C interface slots from ISystem.h and IPerformanceMonitoring.h.
        private const int ReleaseSlot = 1, SystemGetGpus = 1, SystemGetPerformance = 9;
        private const int ListSize = 3, ListAtGpu = 11, PerformanceGetCurrentGpuMetrics = 18;
        private const int MetricsGpuTemperature = 7, MetricsGpuHotspotTemperature = 8;

        private static bool attempted;
        private static IntPtr library, system;
        private static Terminate terminate;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr reserved, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll")]
        private static extern bool FreeLibrary(IntPtr module);

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int QueryVersion(out ulong version);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int Initialize(ulong version, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        private delegate int Terminate();
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int ReleaseInterface(IntPtr instance);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetInterface(IntPtr instance, out IntPtr result);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate uint GetSize(IntPtr list);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetGpu(IntPtr list, uint index, out IntPtr gpu);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetMetrics(IntPtr service, IntPtr gpu, out IntPtr metrics);
        [UnmanagedFunctionPointer(CallingConvention.StdCall)]
        private delegate int GetTemperature(IntPtr metrics, out double degrees);

        private static T Export<T>(string name) where T : class
        {
            IntPtr address = GetProcAddress(library, name);
            return address == IntPtr.Zero ? null : Marshal.GetDelegateForFunctionPointer<T>(address);
        }

        private static T Method<T>(IntPtr instance, int slot) where T : class
        {
            IntPtr table = Marshal.ReadIntPtr(instance);
            return Marshal.GetDelegateForFunctionPointer<T>(Marshal.ReadIntPtr(table, slot * IntPtr.Size));
        }

        private static void Open()
        {
            library = LoadLibraryExW(Environment.Is64BitProcess ? "amdadlx64.dll" : "amdadlx32.dll",
                IntPtr.Zero, LoadLibrarySearchSystem32);
            if (library == IntPtr.Zero) return;
            try
            {
                QueryVersion queryVersion = Export<QueryVersion>("ADLXQueryFullVersion");
                Initialize initialize = Export<Initialize>("ADLXInitialize");
                terminate = Export<Terminate>("ADLXTerminate");
                ulong version;
                if (queryVersion == null || initialize == null || terminate == null || queryVersion(out version) != 0) return;
                ulong major = version >> 48;
                IntPtr result;
                if ((major == 1 || major == 2) && initialize(version, out result) == 0) system = result;
            }
            finally
            {
                if (system == IntPtr.Zero) Close();
            }
        }

        private static void Release(IntPtr instance)
        {
            if (instance != IntPtr.Zero) Method<ReleaseInterface>(instance, ReleaseSlot)(instance);
        }

        private static void ReadMetric(IntPtr metrics, int slot, ref double best)
        {
            double value;
            if (Method<GetTemperature>(metrics, slot)(metrics, out value) == 0 && HardwareEngine.Valid(value))
                best = Math.Max(best, value);
        }

        internal static void Read(out double core, out double hotspot)
        {
            core = hotspot = -1;
            if (!attempted)
            {
                attempted = true;
                Open();
            }
            if (system == IntPtr.Zero) return;

            IntPtr performance = IntPtr.Zero, gpus = IntPtr.Zero;
            try
            {
                if (Method<GetInterface>(system, SystemGetPerformance)(system, out performance) != 0 || performance == IntPtr.Zero ||
                    Method<GetInterface>(system, SystemGetGpus)(system, out gpus) != 0 || gpus == IntPtr.Zero) return;
                uint count = Math.Min(Method<GetSize>(gpus, ListSize)(gpus), 64u);
                GetGpu getGpu = Method<GetGpu>(gpus, ListAtGpu);
                GetMetrics getMetrics = Method<GetMetrics>(performance, PerformanceGetCurrentGpuMetrics);
                for (uint index = 0; index < count; ++index)
                {
                    IntPtr gpu = IntPtr.Zero, metrics = IntPtr.Zero;
                    try
                    {
                        if (getGpu(gpus, index, out gpu) != 0 || gpu == IntPtr.Zero ||
                            getMetrics(performance, gpu, out metrics) != 0 || metrics == IntPtr.Zero) continue;
                        ReadMetric(metrics, MetricsGpuTemperature, ref core);
                        ReadMetric(metrics, MetricsGpuHotspotTemperature, ref hotspot);
                    }
                    finally { Release(metrics); Release(gpu); }
                }
            }
            finally { Release(gpus); Release(performance); }
        }

        private static void Close()
        {
            try { if (system != IntPtr.Zero) terminate(); }
            finally
            {
                system = IntPtr.Zero;
                terminate = null;
                if (library != IntPtr.Zero) FreeLibrary(library);
                library = IntPtr.Zero;
            }
        }

        internal static void Reset()
        {
            Close();
            attempted = false;
        }
    }
}
