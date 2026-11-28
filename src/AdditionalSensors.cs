using System;
using System.Globalization;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Management;
using System.Net;
using System.Net.Security;
using System.Net.WebSockets;
using System.Runtime.InteropServices;
using System.Security.Cryptography.X509Certificates;
using System.Text;
using System.Threading;
using System.Xml;


namespace TaskbarMonitor.Sensors
{
    internal static class AdditionalSensors
    {
        internal static bool Valid(double value) { return !Double.IsNaN(value) && !Double.IsInfinity(value) && value > 0 && value <= 150; }
        private static double Hottest(double current, double value) { return Valid(value) ? Math.Max(current, value) : current; }
        private static void Prefer(double value, int source, bool cpu, ref double current, ref int currentSource)
        {
            if (!Valid(value) || !SensorSource.Better(source, currentSource, cpu)) return;
            current = value;
            currentSource = source;
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
                    error is TypeInitializationException || error is ManagementException ||
                    error is COMException || error is XmlException || error is ArgumentException || error is InvalidOperationException ||
                    error is WaitHandleCannotBeOpenedException)) throw;
            }
        }
        private static DateTime nextProbe;
        private static double cachedCpu = -1, cachedGpu = -1;
        private static int cachedCpuSource, cachedGpuSource;
        internal static void Read(ref double cpu, ref double gpu, ref int cpuSource, ref int gpuSource)
        {
            if (DateTime.UtcNow >= nextProbe)
            {
                cachedCpu = cachedGpu = -1; cachedCpuSource = cachedGpuSource = 0;
                double externalCpu = -1, externalGpu = -1;
                IgnoreUnavailable(delegate { Hwinfo(ref externalCpu, ref externalGpu); });
                Prefer(externalCpu, 9, true, ref cachedCpu, ref cachedCpuSource);
                Prefer(externalGpu, 9, false, ref cachedGpu, ref cachedGpuSource);
                externalCpu = externalGpu = -1;
                IgnoreUnavailable(delegate { Aida(ref externalCpu, ref externalGpu); });
                Prefer(externalCpu, 7, true, ref cachedCpu, ref cachedCpuSource);
                Prefer(externalGpu, 7, false, ref cachedGpu, ref cachedGpuSource);
                foreach (string provider in new[] { "LibreHardwareMonitor", "OpenHardwareMonitor" })
                {
                    externalCpu = externalGpu = -1;
                    IgnoreUnavailable(delegate { Wmi(provider, ref externalCpu, ref externalGpu); });
                    Prefer(externalCpu, 8, true, ref cachedCpu, ref cachedCpuSource);
                    Prefer(externalGpu, 8, false, ref cachedGpu, ref cachedGpuSource);
                }
                NitroSenseTelemetry.Apply(ref cachedCpu, ref cachedGpu, ref cachedCpuSource, ref cachedGpuSource);
                nextProbe = DateTime.UtcNow.AddSeconds(5);
            }
            Prefer(cachedCpu, cachedCpuSource, true, ref cpu, ref cpuSource);
            Prefer(cachedGpu, cachedGpuSource, false, ref gpu, ref gpuSource);
        }
        internal static void Reset() { nextProbe = DateTime.MinValue; cachedCpu = cachedGpu = -1; NitroSenseTelemetry.Stop(); }
        internal static void SelfCheck()
        {
            NitroSenseTelemetry.SelfCheck();
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

    internal static class NitroSenseTelemetry
    {
        internal const int SourceId = 10;
        private const string Topic = "broadcast_PWARuntime";
        private const string EventType = "UPDATE_MONITOR_DATA";
        private const string CertificateThumbprint = "6AA3870C7561C7C4305B1E8F068F581A6A2B9E3B";
        private const int DefaultPort = 15150;
        private static readonly object Sync = new object();
        private static int started, stop, failures;
        private static Thread worker;
        private static ClientWebSocket currentSocket;
        private static double cachedCpu = -1, cachedGpu = -1;
        private static DateTime lastUpdate = DateTime.MinValue;
        private static string lastError = String.Empty;
        private static string certificateError = String.Empty;

        internal static void Apply(ref double cpu, ref double gpu, ref int cpuSource, ref int gpuSource)
        {
            EnsureStarted();
            if (Interlocked.CompareExchange(ref stop, 0, 0) != 0) return;
            double detectedCpu, detectedGpu;
            DateTime updated;
            lock (Sync)
            {
                detectedCpu = cachedCpu;
                detectedGpu = cachedGpu;
                updated = lastUpdate;
            }
            if (DateTime.UtcNow - updated > TimeSpan.FromSeconds(30)) return;
            if (ValidTemperature(detectedCpu) && SensorSource.Better(SourceId, cpuSource, true))
            { cpu = detectedCpu; cpuSource = SourceId; }
            if (ValidTemperature(detectedGpu) && SensorSource.Better(SourceId, gpuSource, false))
            { gpu = detectedGpu; gpuSource = SourceId; }
        }

        internal static void Stop()
        {
            Interlocked.Exchange(ref stop, 1);
            ClientWebSocket socket = currentSocket;
            if (socket != null) { try { socket.Abort(); } catch (Exception) { } }
            Thread thread = worker;
            if (thread != null && thread.IsAlive && !Object.ReferenceEquals(thread, Thread.CurrentThread))
            {
                try { thread.Join(200); } catch (Exception) { }
            }
        }

        private static bool ValidTemperature(double value)
        {
            return !Double.IsNaN(value) && !Double.IsInfinity(value) && value > 0 && value <= 150;
        }

        private static void EnsureStarted()
        {
            try
            {
                if (Interlocked.CompareExchange(ref started, 1, 0) != 0) return;
                Interlocked.Exchange(ref stop, 0);
                Thread thread = new Thread(Loop);
                thread.IsBackground = true;
                thread.Name = "TaskbarMonitor.NitroSenseTelemetry";
                worker = thread;
                thread.Start();
            }
            catch (Exception)
            {
                Interlocked.Exchange(ref started, 0);
            }
        }

        private static void Loop()
        {
            while (Interlocked.CompareExchange(ref stop, 0, 0) == 0)
            {
                try { Listen(); }
                catch (Exception error) { lock (Sync) { lastError = error.ToString(); } }
                if (Interlocked.CompareExchange(ref stop, 0, 0) != 0) break;
                int attempt = Interlocked.Increment(ref failures);
                int backoff = Math.Min(30000, 1000 * (1 << Math.Min(Math.Max(attempt - 1, 0), 5)));
                for (int elapsed = 0; elapsed < backoff && Interlocked.CompareExchange(ref stop, 0, 0) == 0; elapsed += 250)
                    Thread.Sleep(250);
            }
            lock (Sync) { cachedCpu = cachedGpu = -1; lastUpdate = DateTime.MinValue; }
            Interlocked.Exchange(ref started, 0);
        }

        private static void Listen()
        {
            int port = ReadPort();
            Uri uri = new Uri("wss://[::1]:" + port.ToString(CultureInfo.InvariantCulture) + "/mqtt", UriKind.Absolute);
            ClientWebSocket socket = new ClientWebSocket();
            currentSocket = socket;
            try
            {
                socket.Options.KeepAliveInterval = TimeSpan.FromSeconds(20);
                SecurityProtocolType previousProtocol = ServicePointManager.SecurityProtocol;
                RemoteCertificateValidationCallback previousCallback = ServicePointManager.ServerCertificateValidationCallback;
                try
                {
                    ServicePointManager.SecurityProtocol |= SecurityProtocolType.Tls12;
                    ServicePointManager.ServerCertificateValidationCallback = ValidateCertificate;
                    using (CancellationTokenSource timeout = new CancellationTokenSource(TimeSpan.FromSeconds(8)))
                    {
                        socket.ConnectAsync(uri, timeout.Token).GetAwaiter().GetResult();
                        string clientId = "TaskbarMonitor_" + DateTime.UtcNow.Ticks.ToString(CultureInfo.InvariantCulture);
                        SendFrame(socket, BuildConnectFrame(clientId), timeout.Token);
                        MqttFrame connack = ReadFrame(socket, timeout.Token);
                        if (connack.Type != 2 || connack.Payload.Length < 2 || connack.Payload[1] != 0)
                            throw new InvalidOperationException("MQTT CONNACK rejected");
                        SendFrame(socket, BuildSubscribeFrame(Topic), timeout.Token);
                        MqttFrame suback = ReadFrame(socket, timeout.Token);
                        if (suback.Type != 9 || suback.Payload.Length < 3 || suback.Payload[2] > 2)
                            throw new InvalidOperationException("MQTT SUBACK rejected");
                    }
                }
                finally
                {
                    ServicePointManager.ServerCertificateValidationCallback = previousCallback;
                    ServicePointManager.SecurityProtocol = previousProtocol;
                }
                Interlocked.Exchange(ref failures, 0);
                while (Interlocked.CompareExchange(ref stop, 0, 0) == 0)
                {
                    MqttFrame frame = ReadFrame(socket, CancellationToken.None);
                    if (frame.Type == 12)
                    {
                        SendFrame(socket, BuildFrame(0xD0, new byte[0]), CancellationToken.None);
                        continue;
                    }
                    if (frame.Type == 14) throw new InvalidOperationException("MQTT disconnected by broker");
                    if (frame.Type == 3) ProcessPublish(socket, frame);
                }
            }
            finally
            {
                if (Object.ReferenceEquals(currentSocket, socket)) currentSocket = null;
                try { socket.Abort(); } catch (Exception) { }
                try { socket.Dispose(); } catch (Exception) { }
            }
        }

        private static int ReadPort()
        {
            try
            {
                using (Microsoft.Win32.RegistryKey key = Microsoft.Win32.Registry.CurrentUser.OpenSubKey(@"Software\TaskbarMonitor"))
                {
                    object value = key == null ? null : key.GetValue("NitroSenseMqttPort");
                    int port = Convert.ToInt32(value, CultureInfo.InvariantCulture);
                    if (port >= 1024 && port <= 65535) return port;
                }
            }
            catch (Exception) { }
            return DefaultPort;
        }

        private static bool ValidateCertificate(object sender, X509Certificate certificate, X509Chain chain, SslPolicyErrors errors)
        {
            if (certificate == null) { certificateError = "NitroSense certificate missing"; return false; }
            string host = String.Empty;
            HttpWebRequest request = sender as HttpWebRequest;
            if (request != null) { try { host = request.Address.Host; } catch (Exception) { } }
            else
            {
                ServicePoint point = sender as ServicePoint;
                if (point != null) host = point.Address.Host;
            }
            if (host.Length > 0 && !IsLoopbackHost(host))
            {
                certificateError = "NitroSense certificate sender host: " + host;
                return false;
            }
            string thumbprint = certificate.GetCertHashString();
            if (!String.Equals(thumbprint, CertificateThumbprint, StringComparison.OrdinalIgnoreCase))
            {
                certificateError = "NitroSense certificate thumbprint: " + thumbprint;
                return false;
            }
            X509Certificate2 parsed = null;
            try
            {
                parsed = certificate as X509Certificate2;
                if (parsed == null) parsed = new X509Certificate2(certificate);
                string name = parsed.GetNameInfo(X509NameType.SimpleName, false);
                if (!String.Equals(name, "localhost", StringComparison.OrdinalIgnoreCase))
                {
                    certificateError = "NitroSense certificate name: " + name;
                    return false;
                }
                certificateError = "accepted";
                return true;
            }
            catch (Exception error) { certificateError = "NitroSense certificate validation: " + error.Message; return false; }
            finally
            {
                if (parsed != null && !Object.ReferenceEquals(parsed, certificate)) parsed.Dispose();
            }
        }

        private static bool IsLoopbackHost(string host)
        {
            if (host == "localhost") return true;
            if (host.Length > 1 && host[0] == '[' && host[host.Length - 1] == ']')
                host = host.Substring(1, host.Length - 2);
            IPAddress address;
            return IPAddress.TryParse(host, out address) && IPAddress.IsLoopback(address);
        }

        private static byte[] EncodeRemainingLength(int length)
        {
            byte[] bytes = new byte[4];
            int count = 0;
            do
            {
                byte digit = (byte)(length % 128);
                length /= 128;
                if (length > 0) digit |= 0x80;
                bytes[count++] = digit;
            }
            while (length > 0 && count < 4);
            if (length > 0) throw new InvalidOperationException("MQTT packet too large");
            byte[] result = new byte[count];
            Array.Copy(bytes, result, count);
            return result;
        }

        private static byte[] BuildFrame(byte header, byte[] payload)
        {
            byte[] length = EncodeRemainingLength(payload.Length);
            byte[] frame = new byte[1 + length.Length + payload.Length];
            frame[0] = header;
            Array.Copy(length, 0, frame, 1, length.Length);
            Array.Copy(payload, 0, frame, 1 + length.Length, payload.Length);
            return frame;
        }

        private static void WriteUInt16(Stream stream, int value)
        {
            stream.WriteByte((byte)(value >> 8));
            stream.WriteByte((byte)value);
        }

        private static void WriteMqttString(Stream stream, string value)
        {
            byte[] bytes = Encoding.UTF8.GetBytes(value);
            WriteUInt16(stream, bytes.Length);
            stream.Write(bytes, 0, bytes.Length);
        }

        private static byte[] BuildConnectFrame(string clientId)
        {
            using (MemoryStream payload = new MemoryStream())
            {
                WriteMqttString(payload, "MQTT");
                payload.WriteByte(4);
                payload.WriteByte(2);
                WriteUInt16(payload, 60);
                WriteMqttString(payload, clientId);
                return BuildFrame(0x10, payload.ToArray());
            }
        }

        private static byte[] BuildSubscribeFrame(string topic)
        {
            using (MemoryStream payload = new MemoryStream())
            {
                WriteUInt16(payload, 1);
                WriteMqttString(payload, topic);
                payload.WriteByte(0);
                return BuildFrame(0x82, payload.ToArray());
            }
        }

        private static void SendFrame(ClientWebSocket socket, byte[] frame, CancellationToken token)
        {
            socket.SendAsync(new ArraySegment<byte>(frame), WebSocketMessageType.Binary, true, token).GetAwaiter().GetResult();
        }

        private static byte ReadByte(ClientWebSocket socket, CancellationToken token)
        {
            byte[] one = new byte[1];
            while (true)
            {
                WebSocketReceiveResult result = socket.ReceiveAsync(new ArraySegment<byte>(one), token).GetAwaiter().GetResult();
                if (result.MessageType == WebSocketMessageType.Close || result.Count == 0)
                    throw new InvalidOperationException("MQTT WebSocket closed");
                return one[0];
            }
        }

        private static MqttFrame ReadFrame(ClientWebSocket socket, CancellationToken token)
        {
            byte header = ReadByte(socket, token);
            int length = 0, multiplier = 1;
            int digitCount = 0;
            for (; digitCount < 4; ++digitCount)
            {
                byte digit = ReadByte(socket, token);
                length += (digit & 127) * multiplier;
                if ((digit & 128) == 0) break;
                multiplier *= 128;
            }
            if (digitCount == 4 || length < 0) throw new InvalidOperationException("Invalid MQTT frame length");
            byte[] payload = new byte[length];
            for (int i = 0; i < length; ++i) payload[i] = ReadByte(socket, token);
            return new MqttFrame(header >> 4, header & 15, payload);
        }

        private static bool TryReadUInt16(byte[] data, ref int index, out int value)
        {
            value = 0;
            if (index < 0 || index + 2 > data.Length) return false;
            value = (data[index] << 8) | data[index + 1];
            index += 2;
            return true;
        }

        private static bool TryReadMqttString(byte[] data, ref int index, out string value)
        {
            value = String.Empty;
            int length;
            if (!TryReadUInt16(data, ref index, out length) || length < 0 || index + length > data.Length) return false;
            value = Encoding.UTF8.GetString(data, index, length);
            index += length;
            return true;
        }

        private static void ProcessPublish(ClientWebSocket socket, MqttFrame frame)
        {
            int index = 0;
            string topic;
            if (!TryReadMqttString(frame.Payload, ref index, out topic) || topic != Topic) return;
            int packetId = 0;
            int qos = (frame.Flags >> 1) & 3;
            if (qos > 0 && !TryReadUInt16(frame.Payload, ref index, out packetId)) return;
            if (index > frame.Payload.Length) return;
            byte[] message = new byte[frame.Payload.Length - index];
            Array.Copy(frame.Payload, index, message, 0, message.Length);
            byte[] acknowledgement = new[] { (byte)(packetId >> 8), (byte)packetId };
            if (qos == 1) SendFrame(socket, BuildFrame(0x40, acknowledgement), CancellationToken.None);
            else if (qos == 2) SendFrame(socket, BuildFrame(0x50, acknowledgement), CancellationToken.None);
            double cpu, gpu;
            if (!TryParseTelemetry(Encoding.UTF8.GetString(message), out cpu, out gpu)) return;
            lock (Sync)
            {
                cachedCpu = cpu;
                cachedGpu = gpu;
                lastUpdate = DateTime.UtcNow;
            }
        }

        private static bool TryReadJsonValue(string json, string name, out string value)
        {
            value = String.Empty;
            string key = "\"" + name + "\"";
            int index = json.IndexOf(key, StringComparison.Ordinal);
            if (index < 0) return false;
            index += key.Length;
            while (index < json.Length && Char.IsWhiteSpace(json[index])) ++index;
            if (index >= json.Length || json[index] != ':') return false;
            ++index;
            while (index < json.Length && Char.IsWhiteSpace(json[index])) ++index;
            if (index >= json.Length) return false;
            if (json[index] == '"')
            {
                int start = index++;
                bool escaped = false;
                while (index < json.Length)
                {
                    char current = json[index++];
                    if (escaped) { escaped = false; continue; }
                    if (current == '\\') { escaped = true; continue; }
                    if (current == '"')
                    {
                        value = json.Substring(start, index - start);
                        return true;


                    }
                }
                return false;
            }
            int tokenStart = index;
            while (index < json.Length && json[index] != ',' && json[index] != '}' && json[index] != ']' && !Char.IsWhiteSpace(json[index])) ++index;
            value = json.Substring(tokenStart, index - tokenStart);
            return value.Length > 0;
        }

        private static string UnescapeJson(string text)
        {
            StringBuilder result = new StringBuilder();
            for (int i = 0; i < text.Length; ++i)
            {
                char current = text[i];
                if (current != '\\' || i + 1 >= text.Length) { result.Append(current); continue; }
                char escaped = text[++i];
                if (escaped == 'u' && i + 4 < text.Length)
                {
                    int code;
                    if (Int32.TryParse(text.Substring(i + 1, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture, out code))
                    {
                        result.Append((char)code);
                        i += 4;
                        continue;
                    }
                }
                if (escaped == 'b') result.Append('\b');
                else if (escaped == 'f') result.Append('\f');
                else if (escaped == 'n') result.Append('\n');
                else if (escaped == 'r') result.Append('\r');
                else if (escaped == 't') result.Append('\t');
                else result.Append(escaped);
            }
            return result.ToString();
        }

        private static bool TryReadJsonString(string json, string name, out string value)
        {
            string token;
            if (!TryReadJsonValue(json, name, out token) || token.Length < 2 || token[0] != '"' || token[token.Length - 1] != '"')
            {
                value = String.Empty;
                return false;
            }
            value = UnescapeJson(token.Substring(1, token.Length - 2));
            return true;
        }

        private static string JsonScalar(string token)
        {
            if (token.Length > 1 && token[0] == '"' && token[token.Length - 1] == '"')
                return UnescapeJson(token.Substring(1, token.Length - 2));
            return token;
        }

        private static bool Fresh(string timestamp)
        {
            DateTimeOffset offset;
            if (DateTimeOffset.TryParse(timestamp, CultureInfo.InvariantCulture,
                DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out offset))
                return Math.Abs((DateTimeOffset.UtcNow - offset).TotalSeconds) <= 30;
            DateTime exact;
            if (DateTime.TryParseExact(timestamp, new[] { "yyyyMMddHHmmss", "yyyy-MM-dd HH:mm:ss" },
                CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal, out exact))
                return Math.Abs((DateTime.UtcNow - exact).TotalSeconds) <= 30;
            double numeric;
            if (!Double.TryParse(timestamp, NumberStyles.Float, CultureInfo.InvariantCulture, out numeric)) return false;
            double epochMilliseconds;
            if (numeric > 1e11) epochMilliseconds = numeric;
            else if (numeric > 1e9) epochMilliseconds = numeric * 1000;
            else return false;
            double now = (DateTime.UtcNow - new DateTime(1970, 1, 1, 0, 0, 0, DateTimeKind.Utc)).TotalMilliseconds;
            return Math.Abs(now - epochMilliseconds) <= 30000;
        }

        private static bool TryParseTelemetry(string json, out double cpu, out double gpu)
        {
            cpu = gpu = -1;
            string eventType;
            if (!TryReadJsonString(json, "eventType", out eventType) || eventType != EventType) return false;
            string timestamp;
            if (!TryReadJsonValue(json, "timeStamp", out timestamp)) return false;
            if (!Fresh(JsonScalar(timestamp))) return false;
            string text;
            double value;
            if (TryReadJsonValue(json, "CPU_TEMPERATURE", out text) &&
                Double.TryParse(JsonScalar(text), NumberStyles.Float, CultureInfo.InvariantCulture, out value) && ValidTemperature(value)) cpu = value;
            if (TryReadJsonValue(json, "GPU1_TEMPERATURE", out text) &&
                Double.TryParse(JsonScalar(text), NumberStyles.Float, CultureInfo.InvariantCulture, out value) && ValidTemperature(value)) gpu = value;
            return cpu >= 0 || gpu >= 0;
        }

        internal static void SelfCheck()
        {
            byte[] length = EncodeRemainingLength(321);
            if (length.Length != 2 || length[0] != 0xC1 || length[1] != 0x02)
                throw new InvalidOperationException("MQTT remaining length encoding");
            byte[] connect = BuildConnectFrame("TaskbarMonitor_Test");
            if (connect[0] != 0x10 || connect[1] < 4 || connect.Length < 12)
                throw new InvalidOperationException("MQTT CONNECT frame");
            byte[] subscribe = BuildSubscribeFrame(Topic);
            if (subscribe[0] != 0x82 || subscribe[1] <= 0 || subscribe[subscribe.Length - 1] != 0)
                throw new InvalidOperationException("MQTT SUBSCRIBE frame");
            string stamp = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
            string sample = "{\"eventType\":\"UPDATE_MONITOR_DATA\",\"param\":[{\"CPU_TEMPERATURE\":81,\"GPU1_TEMPERATURE\":\"60.5\",\"timeStamp\":\"" + stamp + "\"}],\"sourceClientId\":\"PWARuntime_1\"}";
            double cpu, gpu;
            if (!TryParseTelemetry(sample, out cpu, out gpu) || cpu != 81 || gpu != 60.5)
                throw new InvalidOperationException("NitroSense telemetry parsing");
            if (TryParseTelemetry(sample.Replace(stamp, "2000-01-01T00:00:00Z"), out cpu, out gpu))
                throw new InvalidOperationException("NitroSense telemetry staleness");
            if (TryParseTelemetry(sample.Replace("UPDATE_MONITOR_DATA", "SCREENSHOT"), out cpu, out gpu))
                throw new InvalidOperationException("NitroSense telemetry event filter");
        }

        private sealed class MqttFrame
        {
            internal readonly int Type;
            internal readonly int Flags;
            internal readonly byte[] Payload;

            internal MqttFrame(int type, int flags, byte[] payload)
            {
                Type = type;
                Flags = flags;
                Payload = payload;
            }
        }
    }
}
