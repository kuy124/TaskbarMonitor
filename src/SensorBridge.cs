using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Principal;
using LibreHardwareMonitor.Hardware;

namespace TaskbarMonitor.Sensors
{
    public static class Bootstrap
    {
        // Load only the pinned, embedded dependency closure; never scan for plugins.
        private static readonly string[] Libraries = {
            "System.Buffers.dll", "System.Numerics.Vectors.dll",
            "System.Runtime.CompilerServices.Unsafe.dll", "System.Memory.dll",
            "BlackSharp.Core.dll", "HidSharp.dll", "RAMSPDToolkit-NDD.dll",
            "DiskInfoToolkit.dll", "LibreHardwareMonitorLib.dll"
        };

        public static int Initialize(string directory)
        {
            foreach (string name in Libraries)
                Assembly.Load(File.ReadAllBytes(Path.Combine(directory, name)));
            return 0;
        }

        public static int Read(string unused) { return Reader.Read(); }
        public static int Sources(string unused) { return Reader.Sources(); }
        public static int Status(string unused) { return Reader.Status(); }
        public static int Close(string unused) { Reader.Close(); return 0; }
        public static int SelfCheck(string unused) { return Reader.SelfCheck(); }
    }

    internal static class Reader
    {
        private static Computer computer;
        private static int sourceBits;

        internal static int Read()
        {
            if (computer == null)
            {
                Computer next = new Computer { IsCpuEnabled = true, IsGpuEnabled = true, IsMotherboardEnabled = true };
                try { next.Open(); computer = next; }
                catch { next.Close(); throw; }
            }
            double cpu = -1, gpu = -1;
            int cpuPriority = -1, gpuPriority = -1;
            foreach (IHardware hardware in computer.Hardware)
                Update(hardware, ref cpu, ref cpuPriority, ref gpu, ref gpuPriority);
            if (gpuPriority < 3)
            {
                double driverTemperature = ReadDriverGpuTemperature();
                if (driverTemperature >= 0) Select(driverTemperature, 2, ref gpu, ref gpuPriority);
            }
            int cpuSource = CpuSource(cpuPriority), gpuSource = GpuSource(gpuPriority);
            AdditionalSensors.Read(ref cpu, ref gpu, ref cpuSource, ref gpuSource);
            sourceBits = cpuSource | (gpuSource << 8);
            return Pack(cpu, gpu);
        }

        private static void Update(IHardware hardware, ref double cpu, ref int cpuPriority,
                                   ref double gpu, ref int gpuPriority)
        {
            bool updated = false;
            try { hardware.Update(); updated = true; }
            catch (Exception) { } // One unsupported device must not hide the other devices or its children.
            if (updated) foreach (ISensor sensor in hardware.Sensors)
            {
                if (sensor.SensorType != SensorType.Temperature || !sensor.Value.HasValue) continue;
                double value = sensor.Value.Value;
                if (Double.IsNaN(value) || Double.IsInfinity(value) || value < 0 || value > 150) continue;
                string name = sensor.Name.ToLowerInvariant();
                if (name.Contains("distance")) continue; // A distance to TjMax is not a Celsius reading.
                int cpuRank = CpuSensorPriority(hardware.HardwareType, name);
                if (cpuRank != 0) Select(value, cpuRank, ref cpu, ref cpuPriority);
                int gpuRank = GpuSensorPriority(hardware.HardwareType, name);
                if (gpuRank != 0) Select(value, gpuRank, ref gpu, ref gpuPriority);
            }
            foreach (IHardware child in hardware.SubHardware)
                Update(child, ref cpu, ref cpuPriority, ref gpu, ref gpuPriority);
        }

        private static string SensorName(string name)
        {
            return (name ?? String.Empty).Trim().ToLowerInvariant().Replace('_', ' ').Replace('-', ' ');
        }

        private static bool NumberedName(string name, string prefix)
        {
            if (!name.StartsWith(prefix, StringComparison.Ordinal)) return false;
            string number = name.Substring(prefix.Length).TrimStart('#', ' ');
            if (number.Length == 0) return false;
            foreach (char digit in number) if (digit < '0' || digit > '9') return false;
            return true;
        }

        // Direct CPU priorities remain the source IDs decoded by HardwareSensors.cpp.
        private static int CpuPriority(string name)
        {
            if (String.IsNullOrEmpty(name)) return 0;
            name = SensorName(name);
            if (name.Contains("distance") || name.Contains("tjmax")) return 0;
            if (name == "cpu package" || name == "package" || NumberedName(name, "cpu package ") ||
                NumberedName(name, "package id ")) return 5;
            if (name.Contains("tdie")) return name.Contains("ccd") ? 1 : name.Contains("tctl") ? 3 : 4;
            if (name == "cpu die") return 4;
            if (name == "core average" || name == "cpu core average") return 2;
            if (name == "core max" || name == "cpu core" || NumberedName(name, "cpu core ") ||
                NumberedName(name, "core ")) return 1;
            return 0; // A control offset or an unrelated temperature is not a CPU die reading.
        }

        private static int GpuPriority(string name)
        {
            if (String.IsNullOrEmpty(name)) return 0;
            name = SensorName(name);
            if (name == "gpu core" || name == "core" || name == "gpu temperature" ||
                name == "gpu" || name == "gpu die" || name == "gpu edge" || name == "edge") return 2;
            return name == "gpu hot spot" || name == "gpu hotspot" || name == "hot spot" ||
                   name == "hotspot" || name == "gpu junction" || name == "junction" ||
                   name == "gpu junction temperature" ? 1 : 0;
        }

        private static int GpuSensorPriority(HardwareType type, string name)
        {
            if (type == HardwareType.GpuIntel || type == HardwareType.GpuAmd || type == HardwareType.GpuNvidia)
            {
                int priority = GpuPriority(name);
                return priority == 0 ? 0 : priority + 2;
            }
            if (type != HardwareType.Motherboard && type != HardwareType.SuperIO &&
                type != HardwareType.EmbeddedController) return 0;
            if (String.IsNullOrEmpty(name)) return 0;
            name = SensorName(name);
            return name == "gpu" || name == "gpu temperature" || name == "gpu core" ||
                   name == "gpu diode" || name == "gpu die" || name == "gpu edge" ||
                   name == "igpu" || name == "igpu temperature" || name == "dgpu" ||
                   name == "dgpu temperature" || NumberedName(name, "gpu ") ? 1 : 0;
        }

        private static int GpuSource(int priority)
        {
            return priority <= 0 ? 0 : priority == 1 ? 3 : priority == 2 ? 4 : priority - 2;
        }

        // Windows WDDM adapter performance data; unsupported drivers simply return no reading.
        [StructLayout(LayoutKind.Sequential)]
        private struct AdapterInfo
        {
            internal uint Handle;
            internal uint LuidLow;
            internal int LuidHigh;
            internal uint Sources;
            internal int PrecisePresentRegions;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct AdapterList
        {
            internal uint Count;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 16)]
            internal AdapterInfo[] Adapters;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct AdapterPerformance
        {
            internal uint PhysicalAdapterIndex;
            internal ulong MemoryFrequency, MaxMemoryFrequency, MaxMemoryFrequencyOC, MemoryBandwidth, PcieBandwidth;
            internal uint FanRpm, Power, Temperature;
            internal byte PowerStateOverride;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct AdapterQuery
        {
            internal uint Handle;
            internal int Type;
            internal IntPtr Data;
            internal uint Size;
        }

        [DllImport("gdi32.dll")]
        private static extern int D3DKMTEnumAdapters(ref AdapterList adapters);
        [DllImport("gdi32.dll")]
        private static extern int D3DKMTQueryAdapterInfo(ref AdapterQuery query);
        [DllImport("gdi32.dll")]
        private static extern int D3DKMTCloseAdapter(ref uint handle);

        private static double DriverTemperature(int status, uint temperature)
        {
            return status >= 0 && temperature > 0 && temperature <= 1500 ? temperature / 10.0 : -1;
        }

        private static double ReadDriverGpuTemperature()
        {
            AdapterList adapters = new AdapterList { Adapters = new AdapterInfo[16] };
            IntPtr data = IntPtr.Zero;
            bool opened = false;
            try
            {
                if (D3DKMTEnumAdapters(ref adapters) < 0) return -1;
                opened = true;
                int size = Marshal.SizeOf(typeof(AdapterPerformance));
                data = Marshal.AllocHGlobal(size);
                double hottest = -1;
                for (int i = 0; i < Math.Min(adapters.Count, 16u); ++i)
                {
                    Marshal.StructureToPtr(new AdapterPerformance(), data, false);
                    AdapterQuery query = new AdapterQuery { Handle = adapters.Adapters[i].Handle,
                        Type = 62, Data = data, Size = (uint)size }; // KMTQAITYPE_ADAPTERPERFDATA
                    int status = D3DKMTQueryAdapterInfo(ref query);
                    AdapterPerformance performance = (AdapterPerformance)Marshal.PtrToStructure(data, typeof(AdapterPerformance));
                    hottest = Math.Max(hottest, DriverTemperature(status, performance.Temperature));
                }
                return hottest;
            }
            catch (EntryPointNotFoundException) { return -1; }
            catch (DllNotFoundException) { return -1; }
            finally
            {
                if (data != IntPtr.Zero) Marshal.FreeHGlobal(data);
                if (opened)
                    for (int i = 0; i < Math.Min(adapters.Count, 16u); ++i)
                        D3DKMTCloseAdapter(ref adapters.Adapters[i].Handle);
            }
        }

        private static int CpuSensorPriority(HardwareType hardwareType, string name)
        {
            if (hardwareType == HardwareType.Cpu)
            {
                int priority = CpuPriority(name);
                return priority == 0 ? 0 : priority + 1;
            }
            if (hardwareType != HardwareType.Motherboard && hardwareType != HardwareType.SuperIO &&
                hardwareType != HardwareType.EmbeddedController) return 0;
            if (String.IsNullOrEmpty(name)) return 0;
            name = SensorName(name);
            return name == "cpu" || name == "cpu temperature" || name == "cpu package" ||
                   name == "cpu core" || name == "cpu socket" || name == "cpu diode" ||
                   name == "cpu die" || name == "cpu (peci)" || name == "cpu peci" ||
                   name == "peci" || name == "cpu (tdie)" || name == "cpu (tctl/tdie)" ||
                   NumberedName(name, "cpu ") ? 1 : 0;
        }

        private static int CpuSource(int priority)
        {
            return priority == 1 ? 6 : Math.Max(priority - 1, 0);
        }

        internal static int Sources() { return sourceBits; }

        private static void Select(double value, int priority, ref double result, ref int currentPriority)
        {
            if (priority > currentPriority || (priority == currentPriority && value > result))
            {
                result = value;
                currentPriority = priority;
            }
        }

        private static int Pack(double cpu, double gpu)
        {
            uint c = cpu < 0 ? 65535u : (uint)Math.Round(cpu * 10);
            uint g = gpu < 0 ? 65535u : (uint)Math.Round(gpu * 10);
            return unchecked((int)(c | (g << 16)));
        }

        internal static int Status()
        {
            bool admin = new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
            return (LibreHardwareMonitor.PawnIo.PawnIo.IsInstalled ? 1 : 0) | (admin ? 2 : 0);
        }

        internal static void Close()
        {
            AdditionalSensors.Reset();
            if (computer != null) { computer.Close(); computer = null; }
        }

        internal static int SelfCheck()
        {
            AdditionalSensors.SelfCheck();
            if (typeof(Computer).Assembly.GetName().Version != new Version(0, 9, 6, 0))
                throw new InvalidOperationException("Wrong LibreHardwareMonitor version");
            if (Pack(-1, -1) != -1) throw new InvalidOperationException("Unavailable sensor encoding");
            foreach (string name in new[] { " CPU_Package ", "CPU Package #1", "Package id 0", "CPU Die", "Core 0", "CPU Core 12" })
                if (CpuPriority(name) == 0) throw new InvalidOperationException("CPU temperature alias: " + name);
            foreach (string name in new[] { "Core 0 Critical", "CPU Core #1 TjMax", "CPU Core #1 Voltage", "Package id ", "CPU (Tctl)" })
                if (CpuPriority(name) != 0) throw new InvalidOperationException("Not a CPU reading: " + name);
            foreach (string name in new[] { " GPU_Edge ", "GPU", "GPU Die", "Edge", "GPU Junction Temperature", "Junction" })
                if (GpuPriority(name) == 0) throw new InvalidOperationException("GPU temperature alias: " + name);
            foreach (HardwareType type in new[] { HardwareType.Motherboard, HardwareType.SuperIO, HardwareType.EmbeddedController })
            {
                foreach (string name in new[] { "CPU Diode", "CPU (PECI)", "PECI", "CPU (Tdie)", "CPU #1" })
                    if (CpuSensorPriority(type, name) != 1) throw new InvalidOperationException("CPU fallback alias: " + name);
                foreach (string name in new[] { "dGPU", "GPU Diode", "GPU #1", " GPU_Core " })
                    if (GpuSensorPriority(type, name) != 1) throw new InvalidOperationException("GPU fallback alias: " + name);
                foreach (string name in new[] { "GPU Memory Junction", "GPU VRM", "GPU 0 Critical", "Edge", "Junction" })
                    if (GpuSensorPriority(type, name) != 0) throw new InvalidOperationException("Not a GPU fallback: " + name);
            }
            double temperature = -1;
            int priority = -1;
            Select(95, 1, ref temperature, ref priority);
            Select(60, 3, ref temperature, ref priority);
            Select(99, 1, ref temperature, ref priority);
            Select(65, 3, ref temperature, ref priority);
            if (temperature != 65 || priority != 3) throw new InvalidOperationException("Sensor selection priority");
            if (CpuPriority("CPU Package") != 5 || CpuPriority("CPU (Tdie)") != 4 ||
                CpuPriority("CPU (Tctl/Tdie)") != 3 || CpuPriority("Core Average") != 2 ||
                CpuPriority("CCD1 (Tdie)") != 1 || CpuPriority("CPU (Tctl)") != 0 ||
                CpuPriority("CPU Core #1 Distance to TjMax") != 0 || CpuPriority("CPU Critical Temperature") != 0 ||
                GpuPriority("GPU Core") != 2 || GpuPriority("GPU Hot Spot") != 1 || GpuPriority("GPU Memory") != 0)
                throw new InvalidOperationException("Temperature source classification");
            temperature = -1; priority = -1;
            Select(98, CpuPriority("CCD1 (Tdie)"), ref temperature, ref priority);
            Select(71, CpuPriority("CPU Package"), ref temperature, ref priority);
            if (temperature != 71) throw new InvalidOperationException("Package must outrank a hotter CCD");
            foreach (HardwareType type in new[] { HardwareType.Motherboard, HardwareType.SuperIO, HardwareType.EmbeddedController })
            {
                if (CpuSensorPriority(type, "CPU") != 1 || CpuSensorPriority(type, "CPU Package") != 1 ||
                    CpuSensorPriority(type, "Temperature #1") != 0 || CpuSensorPriority(type, "CPU VRM") != 0 ||
                    CpuSensorPriority(type, "CPU Critical Temperature") != 0 || CpuSensorPriority(type, "CPU Distance to TjMax") != 0)
                    throw new InvalidOperationException("Motherboard CPU fallback classification");
            }
            temperature = -1; priority = -1;
            Select(98, CpuSensorPriority(HardwareType.EmbeddedController, "CPU"), ref temperature, ref priority);
            if (temperature != 98 || CpuSource(priority) != 6) throw new InvalidOperationException("CPU fallback source");
            Select(61, CpuSensorPriority(HardwareType.Cpu, "CPU Core #1"), ref temperature, ref priority);
            Select(99, CpuSensorPriority(HardwareType.SuperIO, "CPU"), ref temperature, ref priority);
            if (temperature != 61 || CpuSource(priority) != 1) throw new InvalidOperationException("Direct CPU must outrank fallback");
            if (CpuSource(-1) != 0 || CpuSource(CpuSensorPriority(HardwareType.Cpu, "CPU Package")) != 5 ||
                CpuSensorPriority(HardwareType.GpuNvidia, "CPU") != 0)
                throw new InvalidOperationException("CPU source encoding");
            if (Marshal.SizeOf(typeof(AdapterInfo)) != 20 || Marshal.SizeOf(typeof(AdapterList)) != 324 ||
                Marshal.SizeOf(typeof(AdapterPerformance)) != 64 ||
                Marshal.OffsetOf(typeof(AdapterPerformance), "Temperature").ToInt32() != 56 ||
                Marshal.SizeOf(typeof(AdapterQuery)) != (IntPtr.Size == 8 ? 24 : 16))
                throw new InvalidOperationException("WDDM ABI layout");
            if (DriverTemperature(0, 615) != 61.5 || DriverTemperature(-1, 615) != -1 ||
                DriverTemperature(0, 0) != -1 || DriverTemperature(0, 1501) != -1)
                throw new InvalidOperationException("WDDM temperature validation");
            foreach (HardwareType type in new[] { HardwareType.GpuIntel, HardwareType.GpuAmd, HardwareType.GpuNvidia })
                if (GpuSource(GpuSensorPriority(type, "GPU Core")) != 2 ||
                    GpuSource(GpuSensorPriority(type, "GPU Hotspot")) != 1 || GpuSensorPriority(type, "GPU Memory") != 0)
                    throw new InvalidOperationException("GPU temperature classification");
            temperature = -1; priority = -1;
            Select(90, GpuSensorPriority(HardwareType.EmbeddedController, "iGPU"), ref temperature, ref priority);
            if (GpuSource(priority) != 3) throw new InvalidOperationException("GPU EC source");
            Select(61, 2, ref temperature, ref priority);
            if (temperature != 61 || GpuSource(priority) != 4) throw new InvalidOperationException("GPU driver source");
            Select(53, GpuSensorPriority(HardwareType.GpuIntel, "GPU Core"), ref temperature, ref priority);
            if (temperature != 53 || GpuSource(priority) != 2 || GpuSource(-1) != 0 ||
                GpuSensorPriority(HardwareType.EmbeddedController, "CPU") != 0 ||
                GpuSensorPriority(HardwareType.SuperIO, "GPU Memory") != 0)
                throw new InvalidOperationException("GPU source priority");
            return Pack(61.5, 53);
        }
    }
}
