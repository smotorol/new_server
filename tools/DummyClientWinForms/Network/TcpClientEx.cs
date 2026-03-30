using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace DummyClientWinForms.Network
{
    public sealed class TcpClientEx : IDisposable
    {
        private TcpClient _client;
        private NetworkStream _stream;
        private CancellationTokenSource _cts;

        public event Action<ushort, byte[]> PacketReceived;
        public event Action<string> Log;
        public event Action<bool> ConnectionChanged;

        public bool IsConnected => _client != null && _client.Connected;

        public async Task ConnectAsync(string host, int port)
        {
            await DisconnectAsync().ConfigureAwait(false);
            _cts = new CancellationTokenSource();
            _client = new TcpClient();
            await _client.ConnectAsync(host, port).ConfigureAwait(false);
            _stream = _client.GetStream();
            ConnectionChanged?.Invoke(true);
            Log?.Invoke($"connected {host}:{port}");
            _ = Task.Run(() => ReadLoopAsync(_cts.Token));
        }

        public async Task DisconnectAsync()
        {
            try { _cts?.Cancel(); } catch { }
            try { _stream?.Dispose(); } catch { }
            try { _client?.Close(); } catch { }
            _stream = null;
            _client = null;
            _cts = null;
            await Task.CompletedTask;
            ConnectionChanged?.Invoke(false);
        }

        public async Task SendAsync(ushort type, byte[] body)
        {
            if (_stream == null) return;
            body = body ?? Array.Empty<byte>();
            var header = new PacketHeader
            {
                Size = (ushort)(PacketHeader.SizeInBytes + body.Length),
                Type = type,
            };
            await _stream.WriteAsync(header.ToBytes(), 0, PacketHeader.SizeInBytes).ConfigureAwait(false);
            if (body.Length > 0)
            {
                await _stream.WriteAsync(body, 0, body.Length).ConfigureAwait(false);
            }
        }

        private async Task ReadLoopAsync(CancellationToken token)
        {
            try
            {
                var headerBytes = new byte[PacketHeader.SizeInBytes];
                while (!token.IsCancellationRequested && _stream != null)
                {
                    await ReadExactAsync(_stream, headerBytes, PacketHeader.SizeInBytes, token).ConfigureAwait(false);
                    var header = PacketHeader.FromBytes(headerBytes);
                    var bodyLen = Math.Max(0, header.Size - PacketHeader.SizeInBytes);
                    var body = new byte[bodyLen];
                    if (bodyLen > 0)
                    {
                        await ReadExactAsync(_stream, body, bodyLen, token).ConfigureAwait(false);
                    }
                    PacketReceived?.Invoke(header.Type, body);
                }
            }
            catch (Exception ex)
            {
                Log?.Invoke("socket closed: " + ex.Message);
            }
            finally
            {
                ConnectionChanged?.Invoke(false);
            }
        }

        private static async Task ReadExactAsync(Stream stream, byte[] buffer, int size, CancellationToken token)
        {
            var read = 0;
            while (read < size)
            {
                var n = await stream.ReadAsync(buffer, read, size - read, token).ConfigureAwait(false);
                if (n <= 0) throw new IOException("remote closed");
                read += n;
            }
        }

        public void Dispose()
        {
            try { DisconnectAsync().GetAwaiter().GetResult(); } catch { }
        }
    }
}
