using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using Microsoft.Win32;
using Microsoft.Win32.SafeHandles;

namespace TaskbarMonitor.Sensors
{
    internal static class IntelGtTemperature
    {
        private const string ModuleResource = "TaskbarMonitor.Sensors.IntelMCHBAR.bin";
        private const uint LoadFromLibraryAndSystem = 0x100 | 0x800; // LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32
        // PP1_TEMPERATURE is the graphics domain's sensor, separate from PKG and PP0.
        private static readonly ulong[] Pp1Temperature = { 0x5980 };
        private static bool attempted;
        private static IntPtr library;
        private static SafeFileHandle executor;
        private static Execute execute;

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        private static extern IntPtr LoadLibraryExW(string path, IntPtr reserved, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, ExactSpelling = true)]
        private static extern IntPtr GetProcAddress(IntPtr module, string name);
        [DllImport("kernel32.dll")]
        private static extern bool FreeLibrary(IntPtr module);

        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int Open(out SafeFileHandle handle);
        [UnmanagedFunctionPointer(CallingConvention.Winapi)]
        private delegate int Load(SafeFileHandle handle, byte[] module, UIntPtr size);
        [UnmanagedFunctionPointer(CallingConvention.Winapi, CharSet = CharSet.Ansi)]
        private delegate int Execute(SafeFileHandle handle, string command, ulong[] input, UIntPtr inputCount,
            out ulong output, UIntPtr outputCount, out UIntPtr returnedCount);

        private static bool SupportedLayout(string identifier)
        {
            Match model = Regex.Match(identifier ?? "", @"^Intel64 Family 6 Model ([0-9]+) Stepping ");
            int number;
            if (!model.Success || !Int32.TryParse(model.Groups[1].Value, out number)) return false;
            switch (number)
            {
                case 0x8E: case 0x9E: // Kaby Lake / Coffee Lake
                case 0x8C: case 0x8D: // Tiger Lake
                case 0x97: case 0x9A: // Alder Lake
                    return true;
                default:
                    return false;
            }
        }

        private static T Export<T>(string name) where T : class
        {
            IntPtr address = GetProcAddress(library, name);
            return address == IntPtr.Zero ? null : Marshal.GetDelegateForFunctionPointer<T>(address);
        }

        internal static bool SupportsProcessor()
        {
            return Environment.Is64BitProcess && SupportedLayout(Registry.GetValue(
                @"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0", "Identifier", null) as string);
        }

        private static void OpenSensor()
        {
            string directory = Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles);
            if (directory.Length == 0) return;
            library = LoadLibraryExW(Path.Combine(directory, @"PawnIO\PawnIOLib.dll"), IntPtr.Zero, LoadFromLibraryAndSystem);
            if (library == IntPtr.Zero) return;
            try
            {
                Open open = Export<Open>("pawnio_open");
                Load load = Export<Load>("pawnio_load");
                Execute read = Export<Execute>("pawnio_execute");
                if (open == null || load == null || read == null || open(out executor) < 0 ||
                    executor == null || executor.IsInvalid) return;
                using (Stream resource = Assembly.GetExecutingAssembly().GetManifestResourceStream(ModuleResource))
                using (MemoryStream buffer = new MemoryStream())
                {
                    if (resource == null) return;
                    resource.CopyTo(buffer);
                    byte[] module = buffer.ToArray();
                    if (load(executor, module, (UIntPtr)module.Length) >= 0) execute = read;
                }
            }
            finally { if (execute == null) Release(); }
        }

        internal static double Read()
        {
            if (!attempted)
            {
                attempted = true;
                OpenSensor();
            }
            if (execute == null) return -1;
            ulong value;
            UIntPtr count;
            int result = execute(executor, "ioctl_read_dword", Pp1Temperature, (UIntPtr)1, out value, (UIntPtr)1, out count);
            return Decode(result, value, count.ToUInt64());
        }

        private static double Decode(int status, ulong value, ulong count)
        {
            return status >= 0 && count == 1 && value > 0 && value <= 150 ? (double)value : -1;
        }

        private static void Release()
        {
            execute = null;
            if (executor != null) { executor.Dispose(); executor = null; }
            if (library != IntPtr.Zero) { FreeLibrary(library); library = IntPtr.Zero; }
        }

        internal static void Reset() { Release(); attempted = false; }

        internal static void SelfCheck()
        {
            using (Stream module = Assembly.GetExecutingAssembly().GetManifestResourceStream(ModuleResource))
                if (module == null || module.Length == 0) throw new InvalidOperationException("Intel GT module is not embedded");
            if (!SupportedLayout("Intel64 Family 6 Model 140 Stepping 1") ||
                SupportedLayout("Intel64 Family 6 Model 255 Stepping 1") || SupportedLayout(null) ||
                Decode(0, 79, 1) != 79 || Decode(0, 0, 1) != -1 || Decode(0, 0xffffffff, 1) != -1 ||
                Decode(-1, 79, 1) != -1 || Decode(0, 79, 0) != -1)
                throw new InvalidOperationException("Intel GT temperature validation");
        }
    }
}
