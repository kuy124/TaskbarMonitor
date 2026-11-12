using System;
using System.IO;
using System.Reflection;
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
                Computer next = new Computer { IsCpuEnabled = true, IsGpuEnabled = true };
                try { next.Open(); computer = next; }
                catch { next.Close(); throw; }
            }
            double cpu = -1, gpu = -1;
            int cpuPriority = -1, gpuPriority = -1;
            foreach (IHardware hardware in computer.Hardware)
                Update(hardware, ref cpu, ref cpuPriority, ref gpu, ref gpuPriority);
            sourceBits = Math.Max(cpuPriority, 0) | (Math.Max(gpuPriority, 0) << 8);
            return Pack(cpu, gpu);
        }

        private static void Update(IHardware hardware, ref double cpu, ref int cpuPriority,
                                   ref double gpu, ref int gpuPriority)
        {
            hardware.Update();
            bool isCpu = hardware.HardwareType == HardwareType.Cpu;
            bool isGpu = hardware.HardwareType == HardwareType.GpuNvidia ||
                         hardware.HardwareType == HardwareType.GpuAmd ||
                         hardware.HardwareType == HardwareType.GpuIntel;
            foreach (ISensor sensor in hardware.Sensors)
            {
                if (sensor.SensorType != SensorType.Temperature || !sensor.Value.HasValue) continue;
                double value = sensor.Value.Value;
                if (Double.IsNaN(value) || Double.IsInfinity(value) || value < 0 || value > 150) continue;
                string name = sensor.Name.ToLowerInvariant();
                if (name.Contains("distance")) continue; // A distance to TjMax is not a Celsius reading.
                if (isCpu)
                {
                    int priority = CpuPriority(name);
                    if (priority != 0) Select(value, priority, ref cpu, ref cpuPriority);
                }
                if (isGpu)
                {
                    int priority = GpuPriority(name);
                    if (priority != 0) Select(value, priority, ref gpu, ref gpuPriority);
                }
            }
            foreach (IHardware child in hardware.SubHardware)
                Update(child, ref cpu, ref cpuPriority, ref gpu, ref gpuPriority);
        }

        // These priorities are also the source IDs decoded by HardwareSensors.cpp.
        private static int CpuPriority(string name)
        {
            if (String.IsNullOrEmpty(name)) return 0;
            name = name.ToLowerInvariant();
            if (name.Contains("distance") || name.Contains("tjmax")) return 0;
            if (name == "cpu package" || name == "package") return 5;
            if (name.Contains("tdie")) return name.Contains("ccd") ? 1 : name.Contains("tctl") ? 3 : 4;
            if (name == "core average" || name == "cpu core average") return 2;
            if (name == "core max" || name == "cpu core" || name.StartsWith("cpu core #") || name.StartsWith("core #")) return 1;
            return 0; // A control offset or an unrelated temperature is not a CPU die reading.
        }

        private static int GpuPriority(string name)
        {
            if (String.IsNullOrEmpty(name)) return 0;
            name = name.ToLowerInvariant();
            if (name == "gpu core" || name == "core" || name == "gpu temperature") return 2;
            return name.Contains("hot spot") || name == "gpu junction" ? 1 : 0;
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
            if (computer != null) { computer.Close(); computer = null; }
        }

        internal static int SelfCheck()
        {
            if (typeof(Computer).Assembly.GetName().Version != new Version(0, 9, 6, 0))
                throw new InvalidOperationException("Wrong LibreHardwareMonitor version");
            if (Pack(-1, -1) != -1) throw new InvalidOperationException("Unavailable sensor encoding");
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
            return Pack(61.5, 53);
        }
    }
}
