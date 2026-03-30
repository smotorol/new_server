using System;

namespace DummyClientWinForms.Network
{
    public struct PacketHeader
    {
        public const int SizeInBytes = 4;

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
            var bytes = new byte[SizeInBytes];
            BitConverter.GetBytes(Size).CopyTo(bytes, 0);
            BitConverter.GetBytes(Type).CopyTo(bytes, 2);
            return bytes;
        }
    }
}
