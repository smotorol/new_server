using System.Threading.Tasks;
using UnityEngine;
using DummyClientUnity.Network;

namespace DummyClientUnity.Runtime
{
    public class DummyClientFlow : MonoBehaviour
    {
        public DummyClientState State = new DummyClientState();
        private readonly TcpClientWrapper _tcp = new TcpClientWrapper();

        public async void Connect()
        {
            State.Connected = await _tcp.ConnectAsync(State.Host, State.Port);
            Debug.Log($"[DummyClientUnity] connected={State.Connected} host={State.Host} port={State.Port}");
        }

        public void Disconnect()
        {
            _tcp.Close();
            ResetGameplayState();
            Debug.Log("[DummyClientUnity] disconnected");
        }

        public async void Reconnect()
        {
            State.ReconnectRequested = true;
            Disconnect();
            await Task.Delay(300);
            Connect();
            Debug.Log("[DummyClientUnity] reconnect requested");
        }

        public void LoginPlaceholder()
        {
            Debug.Log("[DummyClientUnity] protobuf first-path login is ready for activation after Unity Google.Protobuf runtime wiring");
        }

        public void RequestWorldListPlaceholder()
        {
            Debug.Log("[DummyClientUnity] protobuf world-list path is ready for activation after generated C# files are linked into the Unity project");
        }

        public void SelectWorldPlaceholder()
        {
            Debug.Log("[DummyClientUnity] protobuf world-select path is ready for activation after runtime package wiring");
        }

        public void SelectCharacterPlaceholder()
        {
            Debug.Log("[DummyClientUnity] character list/select first-path is the next Unity activation step");
        }

        public void EnterWorldPlaceholder()
        {
            Debug.Log("[DummyClientUnity] world enter first-path will be enabled once protobuf runtime binding is complete");
        }

        private void ResetGameplayState()
        {
            State.Connected = false;
            State.LoggedIn = false;
            State.WorldSelected = false;
            State.CharacterSelected = false;
            State.InWorld = false;
            State.ZoneId = 0;
            State.MapId = 0;
            State.X = 0;
            State.Y = 0;
            State.WorldToken = string.Empty;
            State.LastReason = string.Empty;
            State.WorldEntries.Clear();
            State.Characters.Clear();
        }
    }
}
