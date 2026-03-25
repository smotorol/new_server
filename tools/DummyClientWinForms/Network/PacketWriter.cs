using System;
using System.IO;
using System.Text;

namespace DummyClientWinForms.Network
{
    public sealed class PacketWriter : IDisposable
    {
        private readonly MemoryStream _stream = new MemoryStream();
        private readonly BinaryWriter _writer;

        public PacketWriter()
        {
            _writer = new BinaryWriter(_stream, Encoding.ASCII, true);
        }

        public void WriteUInt16(ushort value) => _writer.Write(value);
        public void WriteUInt32(uint value) => _writer.Write(value);
        public void WriteUInt64(ulong value) => _writer.Write(value);
        public void WriteInt32(int value) => _writer.Write(value);
        public void WriteByte(byte value) => _writer.Write(value);

        public void WriteFixedString(string value, int length)
        {
            var bytes = new byte[length];
            if (!string.IsNullOrEmpty(value))
            {
                var raw = Encoding.ASCII.GetBytes(value);
                Array.Copy(raw, bytes, Math.Min(raw.Length, bytes.Length - 1));
            }
            _writer.Write(bytes);
        }

        public byte[] ToArray() => _stream.ToArray();
        public void Dispose() { _writer.Dispose(); _stream.Dispose(); }
    }
}
