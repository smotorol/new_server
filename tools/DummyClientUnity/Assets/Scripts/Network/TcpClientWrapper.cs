using System;
using System.Net.Sockets;
using System.Threading.Tasks;
using UnityEngine;

namespace DummyClientUnity.Network
{
    public class TcpClientWrapper
    {
        private TcpClient _client;

        public bool Connected => _client != null && _client.Connected;

        public async Task<bool> ConnectAsync(string host, int port)
        {
            try
            {
                _client = new TcpClient();
                await _client.ConnectAsync(host, port);
                return true;
            }
            catch (Exception ex)
            {
                Debug.LogWarning($"[DummyClientUnity] connect failed: {ex.Message}");
                return false;
            }
        }

        public void Close()
        {
            try { _client?.Close(); } catch { }
            _client = null;
        }
    }
}
