using System;
using System.Security.Principal;

namespace TaskbarMonitor.Sensors
{
    public static class Bootstrap
    {
        public static int Initialize(string directory) { return 0; }
        public static int Read(string unused) { return Reader.Read(); }
        public static int Sources(string unused) { return Reader.Sources(); }
        public static int Status(string unused) { return Reader.Status(); }
        public static int Close(string unused) { Reader.Close(); return 0; }
        public static int SelfCheck(string unused) { return Reader.SelfCheck(); }
    }

    internal static class Reader
    {
        private static int sourceBits;

        internal static int Read()
        {
            double cpu = -1, gpu = -1;
            int cpuSource = 0, gpuSource = 0;
            HardwareEngine.Read(ref cpu, ref gpu, ref cpuSource, ref gpuSource);
            AdditionalSensors.Read(ref cpu, ref gpu, ref cpuSource, ref gpuSource);
            sourceBits = cpuSource | (gpuSource << 8);
            return Pack(cpu, gpu);
        }

        internal static int Sources() { return sourceBits; }

        internal static int Status()
        {
            bool admin = new WindowsPrincipal(WindowsIdentity.GetCurrent()).IsInRole(WindowsBuiltInRole.Administrator);
            return (HardwareEngine.Available() ? 1 : 0) | (admin ? 2 : 0);
        }

        internal static void Close()
        {
            AdditionalSensors.Reset();
            HardwareEngine.Reset();
        }

        private static int Pack(double cpu, double gpu)
        {
            uint c = cpu < 0 ? 65535u : (uint)Math.Round(cpu * 10);
            uint g = gpu < 0 ? 65535u : (uint)Math.Round(gpu * 10);
            return unchecked((int)(c | (g << 16)));
        }

        internal static int SelfCheck()
        {
            HardwareEngine.SelfCheck();
            AdditionalSensors.SelfCheck();
            if (Pack(-1, -1) != -1 || Pack(61.5, 53) != (615 | (530 << 16)))
                throw new InvalidOperationException("Temperature encoding");
            if ((14 | (14 << 8)) != 3598)
                throw new InvalidOperationException("Sensor source encoding");
            if (SensorSource.Rank(0, true) != -1 || SensorSource.Rank(15, true) != -1 ||
                SensorSource.Rank(0, false) != -1 || SensorSource.Rank(15, false) != -1)
                throw new InvalidOperationException("Sensor source range");
            return Pack(61.5, 53);
        }
    }
}
