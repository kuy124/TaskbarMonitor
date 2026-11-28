using System;
using System.Globalization;
using System.IO;
using System.Management;
using System.Runtime.InteropServices;
using System.Text.RegularExpressions;
using LibreHardwareMonitor.Hardware;
using LibreHardwareMonitor.PawnIo;
using Microsoft.Win32;

namespace TaskbarMonitor.Sensors
{
    internal static class SensorSource
    {
        internal static int Rank(int source, bool cpu)
        {
            if (cpu)
            {
                switch (source)
                {
                    case 5: return 100;
                    case 4: return 95;
                    case 3: return 90;
                    case 2: return 89;
                    case 1: return 88;
                    case 10: return 87;
                    case 9: return 84;
                    case 7: return 83;
                    case 8: return 75;
                    case 6: return 70;
                    case 11: return 60;
                    case 12: return 50;
                    default: return -1;
                }
            }
            switch (source)
            {
                case 2: return 100;
                case 11: return 99;
                case 12: return 97;
                case 5: return 96;
                case 6: return 95;
                case 1: return 94;
                case 15: return 93;
                case 4: return 91;
                case 10: return 89;
                case 9: return 88;
                case 7: return 87;
                case 8: return 82;
                case 3: return 80;
                case 13: return 50;
                default: return -1;
            }
        }

        internal static bool Better(int candidate, int current, bool cpu)
        {
            return Rank(candidate, cpu) > Rank(current, cpu);
        }
    }

    internal static class HardwareEngine
    {
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

        [StructLayout(LayoutKind.Sequential)]
        private struct ApplicationId
        {
            internal uint Data1;
            internal ushort Data2, Data3;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 8)]
            internal byte[] Data4;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct InitArgs
        {
            internal uint Size;
            internal byte Version;
            internal uint AppVersion;
            internal uint Flags;
            internal uint SupportedVersion;
            internal ApplicationId ApplicationUID;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ApiHandle { internal IntPtr Value; }
        [StructLayout(LayoutKind.Sequential)]
        private struct DeviceHandle { internal IntPtr Value; }
        [StructLayout(LayoutKind.Sequential)]
        private struct IntelTemperatureProperties { internal uint Size; internal byte Version; internal int Type; internal double Maximum; }
        [StructLayout(LayoutKind.Sequential)]
        private struct SysmanTemperatureProperties
        {
            internal uint TypeId; internal IntPtr Next; internal int Type; internal uint OnSubdevice, Subdevice;
            internal double Maximum; internal uint Critical, Threshold1, Threshold2;
        }

        [DllImport("gdi32.dll")]
        private static extern int D3DKMTEnumAdapters(ref AdapterList adapters);
        [DllImport("gdi32.dll")]
        private static extern int D3DKMTQueryAdapterInfo(ref AdapterQuery query);
        [DllImport("gdi32.dll")]
        private static extern int D3DKMTCloseAdapter(ref uint handle);

        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlInit(ref InitArgs arguments, out ApiHandle handle);
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlEnumerateDevices(ApiHandle api, ref uint count, [Out] DeviceHandle[] devices);
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlEnumTemperatureSensors(DeviceHandle device, ref uint count, [Out] IntPtr[] sensors);
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlTemperatureGetProperties(IntPtr sensor, ref IntelTemperatureProperties properties);
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlTemperatureGetState(IntPtr sensor, out double value);
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlClose(ApiHandle api);

        [DllImport("ze_loader.dll", CallingConvention = CallingConvention.Winapi)]
        private static extern int zesInit(uint flags);
        [DllImport("ze_loader.dll", CallingConvention = CallingConvention.Winapi)]
        private static extern int zesDriverGet(ref uint count, [Out] IntPtr[] drivers);
        [DllImport("ze_loader.dll", CallingConvention = CallingConvention.Winapi)]
        private static extern int zesDeviceGet(IntPtr driver, ref uint count, [Out] IntPtr[] devices);
        [DllImport("ze_loader.dll", CallingConvention = CallingConvention.Winapi)]
        private static extern int zesDeviceEnumTemperatureSensors(IntPtr device, ref uint count, [Out] IntPtr[] sensors);
        [DllImport("ze_loader.dll", CallingConvention = CallingConvention.Winapi)]
        private static extern int zesTemperatureGetProperties(IntPtr sensor, ref SysmanTemperatureProperties properties);
        [DllImport("ze_loader.dll", CallingConvention = CallingConvention.Winapi)]
        private static extern int zesTemperatureGetState(IntPtr sensor, out double value);

        [DllImport("nvml.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int nvmlInit_v2();
        [DllImport("nvml.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int nvmlShutdown();
        [DllImport("nvml.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int nvmlDeviceGetCount_v2(out uint count);
        [DllImport("nvml.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int nvmlDeviceGetHandleByIndex_v2(uint index, out IntPtr device);
        [DllImport("nvml.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int nvmlDeviceGetTemperature(IntPtr device, int sensor, out uint temperature);

        private static DateTime nextProbe;
        private static Computer sensorComputer;
        private static readonly Version MinimumPawnIoVersion = new Version(2, 1);
        private static readonly Regex CoreTemperatureName = new Regex(
            @"^(?:(?:CPU )?(?:[PE]-)?Core #[0-9]+|CCD[0-9]+ \(Tdie\))$", RegexOptions.CultureInvariant);
        private static double cachedCpu = -1, cachedGpu = -1;
        private static int cachedCpuSource, cachedGpuSource;
        internal static bool DriverGpuSupported { get; private set; }
        private static bool igclAttempted, igclReady, sysmanAttempted, sysmanReady, nvmlAttempted, nvmlReady;
        private static ApiHandle igclApi;

        internal static bool Valid(double value)
        {
            return !Double.IsNaN(value) && !Double.IsInfinity(value) && value > 0 && value <= 150;
        }

        private static bool SupportsDirectCpu()
        {
            if (!Environment.Is64BitOperatingSystem) return false;
            string vendor = Registry.GetValue(@"HKEY_LOCAL_MACHINE\HARDWARE\DESCRIPTION\System\CentralProcessor\0",
                "VendorIdentifier", null) as string;
            return vendor == "GenuineIntel" || vendor == "AuthenticAMD";
        }

        internal static int CpuDriverStatus()
        {
            int flags = 0;
            TryRead(delegate {
                if (!SupportsDirectCpu()) return;
                flags = 4;
                if (PawnIo.IsInstalled && PawnIo.Version >= MinimumPawnIoVersion) flags |= 1;
            });
            return flags;
        }

        private static double Hottest(double current, double value)
        {
            return Valid(value) ? Math.Max(current, value) : current;
        }

        private static bool TryRead(Action action)
        {
            try { action(); return true; }
            catch (Exception error)
            {
                if (!(error is IOException || error is UnauthorizedAccessException ||
                    error is System.Security.SecurityException || error is DllNotFoundException ||
                    error is EntryPointNotFoundException || error is BadImageFormatException ||
                    error is ManagementException || error is COMException || error is ArgumentException ||
                    error is InvalidOperationException || error is OverflowException ||
                    error is TypeInitializationException || error is NotSupportedException)) throw;
                return false;
            }
        }

        private static void Offer(double value, int source, bool cpu, ref double target, ref int targetSource)
        {
            if (!Valid(value) || !SensorSource.Better(source, targetSource, cpu)) return;
            target = value;
            targetSource = source;
        }

        internal static void Read(ref double cpu, ref double gpu, ref int cpuSource, ref int gpuSource)
        {
            if (DateTime.UtcNow >= nextProbe)
            {
                cachedCpu = cachedGpu = -1;
                cachedCpuSource = cachedGpuSource = 0;
                TryRead(delegate { ReadCpu(ref cachedCpu, ref cachedCpuSource); });
                if (cachedCpuSource == 0)
                {
                    TryRead(delegate { WmiThermalZone(ref cachedCpu, ref cachedCpuSource); });
                    TryRead(delegate { WmiTemperatureProbe(ref cachedCpu, ref cachedCpuSource); });
                }
                TryRead(delegate { WddmGpu(ref cachedGpu, ref cachedGpuSource); });
                TryRead(delegate { IntelGpu(ref cachedGpu, ref cachedGpuSource); });
                TryRead(delegate { SysmanGpu(ref cachedGpu, ref cachedGpuSource); });
                TryRead(delegate { NvmlGpu(ref cachedGpu, ref cachedGpuSource); });
                TryRead(delegate { EmbeddedGpu(ref cachedGpu, ref cachedGpuSource); });
                nextProbe = DateTime.UtcNow.AddSeconds(5);
            }
            Offer(cachedCpu, cachedCpuSource, true, ref cpu, ref cpuSource);
            Offer(cachedGpu, cachedGpuSource, false, ref gpu, ref gpuSource);
        }

        private static int CpuTemperatureSource(string name)
        {
            switch (name)
            {
                case "CPU Package": return 5;
                case "Core (Tdie)": return 4;
                case "Core (Tctl/Tdie)": return 3;
                case "Core Average": return 2;
                case "CPU Core":
                case "CPU Cores":
                case "Core Max":
                case "CCDs Max (Tdie)": return 1;
                default: return CoreTemperatureName.IsMatch(name) ? 1 : 0;
            }
        }

        private static Computer GetSensorComputer()
        {
            if (sensorComputer == null)
            {
                Computer computer = new Computer();
                try { computer.Open(); }
                catch { TryRead(computer.Close); throw; }
                TryRead(delegate { computer.IsCpuEnabled = SupportsDirectCpu(); });
                TryRead(delegate { computer.IsGpuEnabled = true; });
                sensorComputer = computer;
            }
            return sensorComputer;
        }

        private static void ReadCpu(ref double cpu, ref int source)
        {
            if ((CpuDriverStatus() & 1) == 0) return;
            foreach (IHardware hardware in GetSensorComputer().Hardware)
            {
                if (hardware.HardwareType != HardwareType.Cpu || !TryRead(hardware.Update)) continue;
                foreach (ISensor sensor in hardware.Sensors)
                {
                    if (sensor.SensorType != SensorType.Temperature || !sensor.Value.HasValue) continue;
                    int candidate = CpuTemperatureSource(sensor.Name);
                    if (candidate == 0) continue;
                    double value = sensor.Value.Value;
                    if (candidate == source) cpu = Hottest(cpu, value);
                    else Offer(value, candidate, true, ref cpu, ref source);
                }
            }
        }

        private static int GpuTemperatureSource(string name)
        {
            switch (name)
            {
                case "GPU Core":
                case "GPU Edge":
                case "GPU Diode": return 2;
                case "GPU Hot Spot":
                case "GPU Junction": return 1;
                default: return 0;
            }
        }

        private static void EmbeddedGpu(ref double gpu, ref int source)
        {
            DriverGpuSupported = false;
            double core = -1, hotspot = -1;
            bool intelIntegrated = false;
            foreach (IHardware hardware in GetSensorComputer().Hardware)
            {
                HardwareType type = hardware.HardwareType;
                if (type != HardwareType.GpuAmd && type != HardwareType.GpuNvidia && type != HardwareType.GpuIntel) continue;
                if (type == HardwareType.GpuIntel && hardware.Identifier.ToString().StartsWith("/gpu-intel-integrated/", StringComparison.Ordinal))
                    intelIntegrated = true;
                if (!TryRead(hardware.Update)) continue;
                foreach (ISensor sensor in hardware.Sensors)
                {
                    if (sensor.SensorType != SensorType.Temperature || !sensor.Value.HasValue) continue;
                    int candidate = GpuTemperatureSource(sensor.Name);
                    if (candidate == 2) core = Hottest(core, sensor.Value.Value);
                    else if (candidate == 1) hotspot = Hottest(hotspot, sensor.Value.Value);
                }
            }
            Offer(core, 2, false, ref gpu, ref source);
            Offer(hotspot, 1, false, ref gpu, ref source);
            DriverGpuSupported = intelIntegrated && IntelGtTemperature.SupportsProcessor();
            if (source == 0 && DriverGpuSupported && (CpuDriverStatus() & 1) != 0)
                Offer(IntelGtTemperature.Read(), 15, false, ref gpu, ref source);
        }

        private static double Kelvin(double raw)
        {
            double value = raw / 10.0 - 273.15;
            return Valid(value) ? value : -1;
        }

        private static void WmiThermalZone(ref double cpu, ref int source)
        {
            EnumerationOptions options = new EnumerationOptions { Timeout = TimeSpan.FromSeconds(1), ReturnImmediately = false };
            using (ManagementObjectSearcher searcher = new ManagementObjectSearcher(
                new ManagementScope(@"root\WMI"),
                new ObjectQuery("SELECT CurrentTemperature FROM MSAcipi_ThermalZoneTemperature"), options))
            using (ManagementObjectCollection zones = searcher.Get())
                foreach (ManagementObject zone in zones)
                    using (zone)
                    {
                        if (zone["CurrentTemperature"] == null) continue;
                        double value = Kelvin(Convert.ToDouble(zone["CurrentTemperature"], CultureInfo.InvariantCulture));
                        Offer(value, 11, true, ref cpu, ref source);
                    }
        }

        private static void WmiTemperatureProbe(ref double cpu, ref int source)
        {
            EnumerationOptions options = new EnumerationOptions { Timeout = TimeSpan.FromSeconds(1), ReturnImmediately = false };
            using (ManagementObjectSearcher searcher = new ManagementObjectSearcher(
                new ManagementScope(@"root\CIMV2"),
                new ObjectQuery("SELECT CurrentReading, Name FROM Win32_TemperatureProbe"), options))
            using (ManagementObjectCollection probes = searcher.Get())
                foreach (ManagementObject probe in probes)
                    using (probe)
                    {
                        if (probe["CurrentReading"] == null) continue;
                        string name = Convert.ToString(probe["Name"]).ToLowerInvariant();
                        if (name.Length > 0 && name.Contains("gpu")) continue;
                        double value = Kelvin(Convert.ToDouble(probe["CurrentReading"], CultureInfo.InvariantCulture));
                        Offer(value, 12, true, ref cpu, ref source);
                    }
        }

        private static double DriverTemperature(int status, uint temperature)
        {
            return status >= 0 && temperature > 0 && temperature <= 1500 ? temperature / 10.0 : -1;
        }

        private static void WddmGpu(ref double gpu, ref int source)
        {
            AdapterList adapters = new AdapterList { Adapters = new AdapterInfo[16] };
            IntPtr data = IntPtr.Zero;
            bool opened = false;
            try
            {
                if (D3DKMTEnumAdapters(ref adapters) < 0) return;
                opened = true;
                int size = Marshal.SizeOf(typeof(AdapterPerformance));
                data = Marshal.AllocHGlobal(size);
                double result = -1;
                for (int i = 0; i < Math.Min(adapters.Count, 16u); ++i)
                {
                    Marshal.StructureToPtr(new AdapterPerformance(), data, false);
                    AdapterQuery query = new AdapterQuery { Handle = adapters.Adapters[i].Handle,
                        Type = 62, Data = data, Size = (uint)size };
                    int status = D3DKMTQueryAdapterInfo(ref query);
                    AdapterPerformance performance = (AdapterPerformance)Marshal.PtrToStructure(data, typeof(AdapterPerformance));
                    result = Hottest(result, DriverTemperature(status, performance.Temperature));
                }
                Offer(result, 4, false, ref gpu, ref source);
            }
            finally
            {
                if (data != IntPtr.Zero) Marshal.FreeHGlobal(data);
                if (opened)
                    for (int i = 0; i < Math.Min(adapters.Count, 16u); ++i)
                        D3DKMTCloseAdapter(ref adapters.Adapters[i].Handle);
            }
        }

        internal static double IntelTemperature()
        {
            if (!igclAttempted)
            {
                igclAttempted = true;
                try
                {
                    InitArgs arguments = new InitArgs {
                        Size = (uint)Marshal.SizeOf(typeof(InitArgs)),
                        Version = 0,
                        AppVersion = 0x10001,
                        Flags = 1,
                        SupportedVersion = 0,
                        ApplicationUID = new ApplicationId { Data4 = new byte[8] }
                    };
                    ApiHandle api;
                    igclReady = ctlInit(ref arguments, out api) == 0;
                    if (igclReady) igclApi = api;
                }
                catch (Exception error)
                {
                    if (!(error is DllNotFoundException || error is EntryPointNotFoundException ||
                        error is BadImageFormatException)) throw;
                }
            }
            if (!igclReady) return -1;
            uint count = 0;
            if (ctlEnumerateDevices(igclApi, ref count, null) != 0 || count == 0 || count > 64) return -1;
            DeviceHandle[] devices = new DeviceHandle[count];
            if (ctlEnumerateDevices(igclApi, ref count, devices) != 0) return -1;
            double result = -1;
            for (int i = 0; i < Math.Min((int)count, devices.Length); ++i)
            {
                uint sensorCount = 0;
                if (ctlEnumTemperatureSensors(devices[i], ref sensorCount, null) != 0 ||
                    sensorCount == 0 || sensorCount > 64) continue;
                IntPtr[] sensors = new IntPtr[sensorCount];
                if (ctlEnumTemperatureSensors(devices[i], ref sensorCount, sensors) != 0) continue;
                for (int j = 0; j < Math.Min((int)sensorCount, sensors.Length); ++j)
                {
                    IntelTemperatureProperties properties = new IntelTemperatureProperties {
                        Size = (uint)Marshal.SizeOf(typeof(IntelTemperatureProperties))
                    };
                    double value;
                    if (ctlTemperatureGetProperties(sensors[j], ref properties) == 0 && properties.Type == 1 &&
                        ctlTemperatureGetState(sensors[j], out value) == 0) result = Hottest(result, value);
                }
            }
            return result;
        }

        private static void IntelGpu(ref double gpu, ref int source)
        {
            Offer(IntelTemperature(), 5, false, ref gpu, ref source);
        }

        private static double SysmanTemperature()
        {
            if (sysmanAttempted && !sysmanReady) return -1;
            if (!sysmanAttempted)
            {
                sysmanAttempted = true;
                try { sysmanReady = zesInit(0) == 0; }
                catch (Exception error)
                {
                    if (!(error is DllNotFoundException || error is EntryPointNotFoundException ||
                        error is BadImageFormatException)) throw;
                }
            }
            if (!sysmanReady) return -1;
            uint driverCount = 0;
            if (zesDriverGet(ref driverCount, null) != 0 || driverCount == 0 || driverCount > 64) return -1;
            IntPtr[] drivers = new IntPtr[driverCount];
            if (zesDriverGet(ref driverCount, drivers) != 0) return -1;
            double result = -1;
            for (int d = 0; d < Math.Min((int)driverCount, drivers.Length); ++d)
            {
                uint deviceCount = 0;
                if (zesDeviceGet(drivers[d], ref deviceCount, null) != 0 || deviceCount == 0 || deviceCount > 64) continue;
                IntPtr[] devices = new IntPtr[deviceCount];
                if (zesDeviceGet(drivers[d], ref deviceCount, devices) != 0) continue;
                for (int j = 0; j < Math.Min((int)deviceCount, devices.Length); ++j)
                {
                    uint sensorCount = 0;
                    if (zesDeviceEnumTemperatureSensors(devices[j], ref sensorCount, null) != 0 ||
                        sensorCount == 0 || sensorCount > 64) continue;
                    IntPtr[] sensors = new IntPtr[sensorCount];
                    if (zesDeviceEnumTemperatureSensors(devices[j], ref sensorCount, sensors) != 0) continue;
                    for (int i = 0; i < Math.Min((int)sensorCount, sensors.Length); ++i)
                    {
                        SysmanTemperatureProperties properties = new SysmanTemperatureProperties { TypeId = 0x14 };
                        double value;
                        if (zesTemperatureGetProperties(sensors[i], ref properties) == 0 && properties.Type == 1 &&
                            zesTemperatureGetState(sensors[i], out value) == 0) result = Hottest(result, value);
                    }
                }
            }
            return result;
        }

        private static void SysmanGpu(ref double gpu, ref int source)
        {
            Offer(SysmanTemperature(), 6, false, ref gpu, ref source);
        }

        private static void NvmlGpu(ref double gpu, ref int source)
        {
            if (!nvmlAttempted)
            {
                nvmlAttempted = true;
                try { nvmlReady = nvmlInit_v2() == 0; }
                catch (Exception error)
                {
                    if (!(error is DllNotFoundException || error is EntryPointNotFoundException ||
                        error is BadImageFormatException)) throw;
                }
            }
            if (!nvmlReady) return;
            uint count = 0;
            if (nvmlDeviceGetCount_v2(out count) != 0 || count == 0 || count > 64) return;
            double result = -1;
            for (uint i = 0; i < count; ++i)
            {
                IntPtr device;
                uint temperature;
                if (nvmlDeviceGetHandleByIndex_v2(i, out device) == 0 &&
                    nvmlDeviceGetTemperature(device, 0, out temperature) == 0)
                    result = Hottest(result, temperature);
            }
            Offer(result, 11, false, ref gpu, ref source);
        }

        internal static void Reset()
        {
            nextProbe = DateTime.MinValue;
            cachedCpu = cachedGpu = -1;
            cachedCpuSource = cachedGpuSource = 0;
            IntelGtTemperature.Reset();
            DriverGpuSupported = false;
            if (sensorComputer != null)
            {
                TryRead(sensorComputer.Close);
                sensorComputer = null;
            }
            if (igclReady)
            {
                try { ctlClose(igclApi); }
                catch (Exception error)
                {
                    if (!(error is DllNotFoundException || error is EntryPointNotFoundException ||
                        error is BadImageFormatException)) throw;
                }
            }
            igclReady = false;
            igclAttempted = false;
            if (nvmlReady)
            {
                try { nvmlShutdown(); }
                catch (Exception error)
                {
                    if (!(error is DllNotFoundException || error is EntryPointNotFoundException ||
                        error is BadImageFormatException)) throw;
                }
            }
            nvmlReady = false;
            nvmlAttempted = false;
            sysmanReady = false;
            sysmanAttempted = false;
        }

        internal static void SelfCheck()
        {
            IntelGtTemperature.SelfCheck();
            if (GpuTemperatureSource("GPU Core") != 2 || GpuTemperatureSource("GPU Hot Spot") != 1 ||
                GpuTemperatureSource("GPU Memory Junction") != 0 || GpuTemperatureSource("GPU VR SoC") != 0 ||
                GpuTemperatureSource("CPU Package") != 0 || !SensorSource.Better(15, 9, false) ||
                !SensorSource.Better(2, 15, false))
                throw new InvalidOperationException("Direct GPU sensor classification and priority");
            if (CpuTemperatureSource("CPU Package") != 5 || CpuTemperatureSource("Core (Tdie)") != 4 ||
                CpuTemperatureSource("Core (Tctl/Tdie)") != 3 || CpuTemperatureSource("P-Core #1") != 1 ||
                CpuTemperatureSource("CCD1 (Tdie)") != 1 || CpuTemperatureSource("CPU GT Cores") != 0 ||
                CpuTemperatureSource("CPU Core #1 Distance to TjMax") != 0 || CpuTemperatureSource("Core (Tctl)") != 0 ||
                !SensorSource.Better(1, 10, true) || !SensorSource.Better(1, 9, true))
                throw new InvalidOperationException("Direct CPU sensor classification and priority");
            if (Math.Abs(Kelvin(3232) - 50.05) > 0.001 || Kelvin(200) != -1 || Kelvin(5000) != -1)
                throw new InvalidOperationException("Thermal zone conversion");
            if (!Valid(49.85) || Valid(0) || Valid(Double.NaN) || Valid(151))
                throw new InvalidOperationException("Hardware reading validation");
            if (Marshal.SizeOf(typeof(AdapterInfo)) != 20 || Marshal.SizeOf(typeof(AdapterList)) != 324 ||
                Marshal.SizeOf(typeof(AdapterPerformance)) != 64 ||
                Marshal.OffsetOf(typeof(AdapterPerformance), "Temperature").ToInt32() != 56 ||
                Marshal.SizeOf(typeof(AdapterQuery)) != (IntPtr.Size == 8 ? 24 : 16))
                throw new InvalidOperationException("WDDM ABI layout");
            if (DriverTemperature(0, 615) != 61.5 || DriverTemperature(-1, 615) != -1 ||
                DriverTemperature(0, 0) != -1 || DriverTemperature(0, 1501) != -1)
                throw new InvalidOperationException("WDDM temperature validation");
            if (Marshal.SizeOf(typeof(InitArgs)) != 36 || Marshal.OffsetOf(typeof(InitArgs), "ApplicationUID").ToInt32() != 20 ||
                Marshal.SizeOf(typeof(IntelTemperatureProperties)) != 24)
                throw new InvalidOperationException("Intel IGCL ABI layout");
            if (Marshal.SizeOf(typeof(SysmanTemperatureProperties)) != (IntPtr.Size == 8 ? 56 : 48) ||
                Marshal.OffsetOf(typeof(SysmanTemperatureProperties), "Maximum").ToInt32() != (IntPtr.Size == 8 ? 32 : 24))
                throw new InvalidOperationException("Intel Sysman ABI layout");
            if (SensorSource.Rank(5, true) <= SensorSource.Rank(11, true) ||
                SensorSource.Rank(10, true) <= SensorSource.Rank(11, true) ||
                SensorSource.Rank(2, false) <= SensorSource.Rank(3, false) ||
                SensorSource.Rank(11, false) <= SensorSource.Rank(4, false) ||
                !SensorSource.Better(10, 11, true) || SensorSource.Better(11, 10, true) ||
                !SensorSource.Better(11, 4, false) || SensorSource.Better(4, 11, false))
                throw new InvalidOperationException("Sensor source priority");
            double cpu = -1, gpu = -1;
            int cpuSource = 0, gpuSource = 0;
            Offer(55, 11, true, ref cpu, ref cpuSource);
            Offer(60, 5, true, ref cpu, ref cpuSource);
            Offer(58, 10, true, ref cpu, ref cpuSource);
            if (cpu != 60 || cpuSource != 5) throw new InvalidOperationException("CPU provider priority");
            Offer(70, 4, false, ref gpu, ref gpuSource);
            Offer(66, 11, false, ref gpu, ref gpuSource);
            if (gpu != 66 || gpuSource != 11) throw new InvalidOperationException("GPU provider priority");
        }
    }
}
