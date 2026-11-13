using System;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Management;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Xml;

namespace TaskbarMonitor.Sensors
{
    internal static class AdditionalSensors
    {
        [StructLayout(LayoutKind.Sequential)]
        private struct IntelTemperatureProperties { public uint Size; public byte Version; public int Type; public double Maximum; }
        [StructLayout(LayoutKind.Sequential)]
        private struct SysmanTemperatureProperties
        {
            public uint TypeId; public IntPtr Next; public int Type; public uint OnSubdevice, Subdevice;
            public double Maximum; public uint Critical, Threshold1, Threshold2;
        }
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlEnumTemperatureSensors(IntPtr device, ref uint count, [Out] IntPtr[] sensors);
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlTemperatureGetProperties(IntPtr sensor, ref IntelTemperatureProperties properties);
        [DllImport("ControlLib.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int ctlTemperatureGetState(IntPtr sensor, out double value);
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

        internal static bool Valid(double value) { return !Double.IsNaN(value) && !Double.IsInfinity(value) && value > 0 && value <= 150; }
        private static double Hottest(double current, double value) { return Valid(value) ? Math.Max(current, value) : current; }
        private static IntPtr IntelHandle(object device)
        { return (IntPtr)device.GetType().GetField("pNext", BindingFlags.Instance | BindingFlags.NonPublic).GetValue(device); }

        private static double IntelTemperature()
        {
            // Reuse LHM's IGCL context rather than initializing a competing context.
            Type type = typeof(LibreHardwareMonitor.Hardware.Computer).Assembly.GetType("LibreHardwareMonitor.Interop.IntelGcl");
            if (type == null) return -1;
            Array devices = (Array)type.GetMethod("GetDeviceHandles", BindingFlags.Public | BindingFlags.Static).Invoke(null, null);
            double result = -1;
            foreach (object device in devices)
            {
                IntPtr handle = IntelHandle(device);
                uint count = 0;
                if (ctlEnumTemperatureSensors(handle, ref count, null) != 0 || count == 0 || count > 64) continue;
                IntPtr[] sensors = new IntPtr[count];
                if (ctlEnumTemperatureSensors(handle, ref count, sensors) != 0) continue;
                for (int i = 0; i < Math.Min(count, (uint)sensors.Length); ++i)
                {
                    IntelTemperatureProperties properties = new IntelTemperatureProperties { Size = (uint)Marshal.SizeOf(typeof(IntelTemperatureProperties)) };
                    double value;
                    if (ctlTemperatureGetProperties(sensors[i], ref properties) == 0 && properties.Type == 1 &&
                        ctlTemperatureGetState(sensors[i], out value) == 0) result = Hottest(result, value);
                }
            }
            return result;
        }

        private static double SysmanTemperature()
        {
            if (zesInit(0) != 0) return -1;
            uint count = 0;
            if (zesDriverGet(ref count, null) != 0 || count == 0 || count > 64) return -1;
            IntPtr[] drivers = new IntPtr[count];
            if (zesDriverGet(ref count, drivers) != 0) return -1;
            double result = -1;
            for (int d = 0; d < Math.Min(count, (uint)drivers.Length); ++d)
            {
                uint deviceCount = 0;
                if (zesDeviceGet(drivers[d], ref deviceCount, null) != 0 || deviceCount == 0 || deviceCount > 64) continue;
                IntPtr[] devices = new IntPtr[deviceCount];
                if (zesDeviceGet(drivers[d], ref deviceCount, devices) != 0) continue;
                for (int j = 0; j < Math.Min(deviceCount, (uint)devices.Length); ++j)
                {
                    uint sensorCount = 0;
                    if (zesDeviceEnumTemperatureSensors(devices[j], ref sensorCount, null) != 0 || sensorCount == 0 || sensorCount > 64) continue;
                    IntPtr[] sensors = new IntPtr[sensorCount];
                    if (zesDeviceEnumTemperatureSensors(devices[j], ref sensorCount, sensors) != 0) continue;
                    for (int i = 0; i < Math.Min(sensorCount, (uint)sensors.Length); ++i)
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

        internal static int Target(string name)
        {
            name = (name ?? "").ToLowerInvariant();
            if (name.Contains("memory") || name.Contains("vram") || name.Contains("vrm") || name.Contains("distance") ||
                name.Contains("limit") || name.Contains("critical") || name.Contains("tjmax") || name.Contains("maximum")) return 0;
            if (name == "cpu gt cores" || name == "gt cores" || name == "gpu" || name == "igpu" || name == "dgpu" ||
                name.StartsWith("gpu core") || name.StartsWith("gpu diode") || name.StartsWith("gpu temperature") ||
                name.StartsWith("gpu hot") || name.StartsWith("gpu junction") || name.StartsWith("gpu edge")) return 2;
            if (name == "cpu" || name == "cpu temperature" || name.StartsWith("cpu package") || name.StartsWith("cpu core") ||
                name.StartsWith("cpu ia cores") || name.StartsWith("cpu (tdie)") || name.StartsWith("cpu (tctl/tdie)")) return 1;
            return 0;
        }
        private static void Reading(string name, double value, ref double cpu, ref double gpu)
        {
            int target = Target(name);
            if (target == 1) cpu = Hottest(cpu, value);
            if (target == 2) gpu = Hottest(gpu, value);
        }
        internal static void ParseAida(string xml, ref double cpu, ref double gpu)
        {
            XmlReaderSettings settings = new XmlReaderSettings { ConformanceLevel = ConformanceLevel.Fragment,
                DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 1024 * 1024 };
            using (XmlReader reader = XmlReader.Create(new StringReader(xml), settings))
                while (reader.Read())
                {
                    if (reader.NodeType != XmlNodeType.Element || reader.Name != "temp") continue;
                    XmlDocument document = new XmlDocument { XmlResolver = null };
                    using (XmlReader subtree = reader.ReadSubtree()) document.Load(subtree);
                    XmlNode label = document.SelectSingleNode("/temp/label"), value = document.SelectSingleNode("/temp/value");
                    double temperature;
                    if (label != null && value != null && Double.TryParse(value.InnerText, NumberStyles.Float,
                        CultureInfo.InvariantCulture, out temperature)) Reading(label.InnerText, temperature, ref cpu, ref gpu);
                }
        }
        private static void Aida(ref double cpu, ref double gpu)
        {
            using (MemoryMappedFile mapping = MemoryMappedFile.OpenExisting("AIDA64_SensorValues", MemoryMappedFileRights.Read))
            using (MemoryMappedViewAccessor view = mapping.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read))
            {
                int length = (int)Math.Min(view.Capacity, 1024 * 1024);
                byte[] bytes = new byte[length]; view.ReadArray(0, bytes, 0, length);
                int end = Array.IndexOf(bytes, (byte)0);
                if (end < 0) return;
                ParseAida(Encoding.Default.GetString(bytes, 0, end), ref cpu, ref gpu);
            }
        }
        private static void Wmi(string provider, ref double cpu, ref double gpu)
        {
            EnumerationOptions options = new EnumerationOptions { Timeout = TimeSpan.FromSeconds(1), ReturnImmediately = false };
            using (ManagementObjectSearcher searcher = new ManagementObjectSearcher(new ManagementScope(@"root\" + provider),
                new ObjectQuery("SELECT Name, Value, Parent FROM Sensor WHERE SensorType = 'Temperature'"), options))
            using (ManagementObjectCollection sensors = searcher.Get())
                foreach (ManagementObject sensor in sensors)
                    using (sensor)
                    {
                        string parent = Convert.ToString(sensor["Parent"]).ToLowerInvariant();
                        string name = Convert.ToString(sensor["Name"]);
                        int target = Target(name);
                        if ((target == 1 && (parent.StartsWith("/intelcpu/") || parent.StartsWith("/amdcpu/"))) ||
                            (target == 2 && parent.StartsWith("/gpu-")))
                            if (sensor["Value"] != null) Reading(name, Convert.ToDouble(sensor["Value"], CultureInfo.InvariantCulture), ref cpu, ref gpu);
                    }
        }
        private static string Label(byte[] data, int offset, int length)
        {
            int end = Array.IndexOf(data, (byte)0, offset, length);
            return Encoding.Default.GetString(data, offset, end < 0 ? length : end - offset);
        }
        private static bool Section(byte[] data, uint offset, uint size, uint count, uint minimum)
        { return size >= minimum && count <= 4096 && (ulong)offset + (ulong)size * count <= (ulong)data.Length; }

        internal static void ParseHwinfo(byte[] data, long now, ref double cpu, ref double gpu)
        {
            if (data.Length < 44 || BitConverter.ToUInt32(data, 0) != 0x53695748 || BitConverter.ToUInt32(data, 4) != 2) return;
            long poll = BitConverter.ToInt64(data, 12);
            if (poll > now + 5 || poll < now - 30) return;
            uint sensors = BitConverter.ToUInt32(data, 20), sensorSize = BitConverter.ToUInt32(data, 24), sensorCount = BitConverter.ToUInt32(data, 28);
            uint readings = BitConverter.ToUInt32(data, 32), readingSize = BitConverter.ToUInt32(data, 36), readingCount = BitConverter.ToUInt32(data, 40);
            if (!Section(data, sensors, sensorSize, sensorCount, 264) || !Section(data, readings, readingSize, readingCount, 316)) return;
            for (uint i = 0; i < readingCount; ++i)
            {
                int offset = (int)(readings + i * readingSize);
                if (BitConverter.ToUInt32(data, offset) != 1) continue;
                uint owner = BitConverter.ToUInt32(data, offset + 4);
                if (owner >= sensorCount) continue;
                string name = Label(data, offset + 12, 128);
                string hardware = Label(data, (int)(sensors + owner * sensorSize + 8), 128).ToLowerInvariant();
                if (name.StartsWith("Core ", StringComparison.OrdinalIgnoreCase) && hardware.Contains("cpu")) name = "CPU " + name;
                Reading(name, BitConverter.ToDouble(data, offset + 284), ref cpu, ref gpu);
            }
        }
        private static void Hwinfo(ref double cpu, ref double gpu)
        {
            using (Mutex mutex = Mutex.OpenExisting(@"Global\HWiNFO_SM2_MUTEX"))
            {
                bool owned = false;
                try
                {
                    try { owned = mutex.WaitOne(50); } catch (AbandonedMutexException) { owned = true; }
                    if (!owned) return;
                    using (var map = MemoryMappedFile.OpenExisting(@"Global\HWiNFO_SENS_SM2", MemoryMappedFileRights.Read))
                    using (var view = map.CreateViewAccessor(0, 0, MemoryMappedFileAccess.Read))
                    {
                        if (view.Capacity < 44) return;
                        // HWiNFO reserves a large mapping; section bounds validate the populated prefix.
                        byte[] data = new byte[(int)Math.Min(view.Capacity, 4 * 1024 * 1024)]; view.ReadArray(0, data, 0, data.Length);
                        ParseHwinfo(data, (long)(DateTime.UtcNow - new DateTime(1970, 1, 1)).TotalSeconds, ref cpu, ref gpu);
                    }
                }
                finally { if (owned) mutex.ReleaseMutex(); }
            }
        }
        // Optional providers must never prevent the remaining backends from running.
        private static void IgnoreUnavailable(Action read)
        {
            try { read(); }
            catch (Exception error)
            {
                if (!(error is IOException || error is UnauthorizedAccessException || error is System.Security.SecurityException ||
                    error is DllNotFoundException || error is EntryPointNotFoundException || error is BadImageFormatException ||
                    error is TargetInvocationException || error is TypeInitializationException || error is ManagementException ||
                    error is COMException || error is XmlException || error is ArgumentException || error is InvalidOperationException ||
                    error is WaitHandleCannotBeOpenedException)) throw;
            }
        }
        private static DateTime nextProbe;
        private static double cachedCpu = -1, cachedGpu = -1;
        private static int cachedCpuSource, cachedGpuSource;
        internal static void Read(ref double cpu, ref double gpu, ref int cpuSource, ref int gpuSource)
        {
            if (cpu >= 0 && gpu >= 0) return;
            if (DateTime.UtcNow >= nextProbe)
            {
                cachedCpu = cachedGpu = -1; cachedCpuSource = cachedGpuSource = 0;
                if (gpu < 0)
                {
                    IgnoreUnavailable(delegate { cachedGpu = IntelTemperature(); if (Valid(cachedGpu)) cachedGpuSource = 5; });
                    if (cachedGpu < 0) IgnoreUnavailable(delegate { cachedGpu = SysmanTemperature(); if (Valid(cachedGpu)) cachedGpuSource = 6; });
                }
                double externalCpu = -1, externalGpu = -1;
                IgnoreUnavailable(delegate { Hwinfo(ref externalCpu, ref externalGpu); });
                if (Valid(externalCpu)) { cachedCpu = externalCpu; cachedCpuSource = 9; }
                if (cachedGpu < 0 && Valid(externalGpu)) { cachedGpu = externalGpu; cachedGpuSource = 9; }
                externalCpu = externalGpu = -1;
                IgnoreUnavailable(delegate { Aida(ref externalCpu, ref externalGpu); });
                if (cachedCpu < 0 && Valid(externalCpu)) { cachedCpu = externalCpu; cachedCpuSource = 7; }
                if (cachedGpu < 0 && Valid(externalGpu)) { cachedGpu = externalGpu; cachedGpuSource = 7; }
                foreach (string provider in new[] { "LibreHardwareMonitor", "OpenHardwareMonitor" })
                {
                    if (cachedCpu >= 0 && cachedGpu >= 0) break;
                    externalCpu = externalGpu = -1;
                    IgnoreUnavailable(delegate { Wmi(provider, ref externalCpu, ref externalGpu); });
                    if (cachedCpu < 0 && Valid(externalCpu)) { cachedCpu = externalCpu; cachedCpuSource = 8; }
                    if (cachedGpu < 0 && Valid(externalGpu)) { cachedGpu = externalGpu; cachedGpuSource = 8; }
                }
                // ponytail: optional providers polled every 5 seconds; shorten if faster fallback readings matter.
                nextProbe = DateTime.UtcNow.AddSeconds(5);
            }
            if (cpu < 0 && Valid(cachedCpu)) { cpu = cachedCpu; cpuSource = cachedCpuSource; }
            if (gpu < 0 && Valid(cachedGpu)) { gpu = cachedGpu; gpuSource = cachedGpuSource; }
        }
        internal static void Reset() { nextProbe = DateTime.MinValue; cachedCpu = cachedGpu = -1; }
        internal static void SelfCheck()
        {
            Type handleType = typeof(LibreHardwareMonitor.Hardware.Computer).Assembly.GetType("LibreHardwareMonitor.Interop.IntelGcl+ctl_device_adapter_handle_t");
            object handle = Activator.CreateInstance(handleType);
            handleType.GetField("pNext", BindingFlags.Instance | BindingFlags.NonPublic).SetValue(handle, new IntPtr(123));
            if (IntelHandle(handle) != new IntPtr(123)) throw new InvalidOperationException("Pinned IGCL handle ABI");
            if (Marshal.SizeOf(typeof(IntelTemperatureProperties)) != 24 ||
                Marshal.OffsetOf(typeof(SysmanTemperatureProperties), "Maximum").ToInt32() != (IntPtr.Size == 8 ? 32 : 24))
                throw new InvalidOperationException("Temperature API ABI");
            double cpu = -1, gpu = -1;
            ParseAida("<temp><label>CPU Package</label><value>61.5</value></temp><temp><label>CPU GT Cores</label><value>53</value></temp><temp><label>GPU Memory</label><value>99</value></temp>", ref cpu, ref gpu);
            if (cpu != 61.5 || gpu != 53 || Target("CPU GT Cores") != 2 || Target("GPU Memory") != 0 ||
                Target("CPU Package Maximum") != 0 || Valid(Double.NaN) || Valid(0) || Valid(151))
                throw new InvalidOperationException("External sensor validation");
            byte[] data = new byte[624];
            Action<int, uint> put = delegate(int offset, uint value) { Array.Copy(BitConverter.GetBytes(value), 0, data, offset, 4); };
            put(0, 0x53695748); put(4, 2); Array.Copy(BitConverter.GetBytes(1000L), 0, data, 12, 8);
            put(20, 44); put(24, 264); put(28, 1); put(32, 308); put(36, 316); put(40, 1); put(308, 1);
            Array.Copy(Encoding.ASCII.GetBytes("CPU GT Cores"), 0, data, 320, 12);
            Array.Copy(BitConverter.GetBytes(54.0), 0, data, 592, 8);
            cpu = gpu = -1; ParseHwinfo(data, 1000, ref cpu, ref gpu);
            if (cpu != -1 || gpu != 54) throw new InvalidOperationException("HWiNFO GT sensor");
            gpu = -1; ParseHwinfo(data, 1100, ref cpu, ref gpu);
            if (gpu != -1) throw new InvalidOperationException("HWiNFO stale data");
            put(36, UInt32.MaxValue); ParseHwinfo(data, 1000, ref cpu, ref gpu);
            if (gpu != -1) throw new InvalidOperationException("HWiNFO invalid bounds");
            put(36, 460); put(24, 392); put(32, 436);
            byte[] reserved = new byte[20 * 1024 * 1024];
            Array.Copy(data, reserved, data.Length);
            Array.Copy(data, 308, reserved, 436, 316);
            cpu = gpu = -1; ParseHwinfo(reserved, 1000, ref cpu, ref gpu);
            if (gpu != 54) throw new InvalidOperationException("HWiNFO reserved mapping");
        }
    }
}
