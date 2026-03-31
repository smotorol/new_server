using System;
using System.Text;

namespace DummyClientWinForms.Network
{
    public static class PacketReader
    {
        public static ushort ReadUInt16(byte[] buffer, int offset) => BitConverter.ToUInt16(buffer, offset);
        public static uint ReadUInt32(byte[] buffer, int offset) => BitConverter.ToUInt32(buffer, offset);
        public static ulong ReadUInt64(byte[] buffer, int offset) => BitConverter.ToUInt64(buffer, offset);
        public static int ReadInt32(byte[] buffer, int offset) => BitConverter.ToInt32(buffer, offset);
        public static string ReadFixedString(byte[] buffer, int offset, int count)
        {
            var raw = Encoding.ASCII.GetString(buffer, offset, count);
            var zero = raw.IndexOf('\0');
            return zero >= 0 ? raw.Substring(0, zero) : raw.TrimEnd('\0');
        }
    }
}
