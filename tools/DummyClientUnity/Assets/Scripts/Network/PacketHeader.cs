using System;
using System.IO;

namespace DummyClientUnity.Network
{
    public struct PacketHeader
    {
        public ushort Size;
        public ushort Type;

        public static PacketHeader FromBytes(byte[] buffer)
        {
            return new PacketHeader
            {
                Size = BitConverter.ToUInt16(buffer, 0),
                Type = BitConverter.ToUInt16(buffer, 2),
            };
        }

        public byte[] ToBytes()
        {
            using (var ms = new MemoryStream())
            using (var bw = new BinaryWriter(ms))
            {
                bw.Write(Size);
                bw.Write(Type);
                return ms.ToArray();
            }
        }
    }
}
