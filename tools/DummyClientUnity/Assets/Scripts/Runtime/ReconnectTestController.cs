using UnityEngine;

namespace DummyClientUnity.Runtime
{
    public class ReconnectTestController : MonoBehaviour
    {
        public DummyClientState State = new DummyClientState();
        public DummyClientFlow Flow;

        private void OnApplicationPause(bool pause)
        {
            Debug.Log($"[DummyClientUnity] OnApplicationPause pause={pause}");
            if (pause)
            {
                State.ReconnectRequested = true;
            }
        }

        private void OnApplicationFocus(bool focus)
        {
            Debug.Log($"[DummyClientUnity] OnApplicationFocus focus={focus}");
            if (focus && State.ReconnectRequested && Flow != null)
            {
                Flow.Reconnect();
            }
        }

        public void ForceDisconnect()
        {
            Debug.Log("[DummyClientUnity] force disconnect requested");
            State.Connected = false;
            State.InWorld = false;
            State.ReconnectRequested = true;
        }

        public void SimulateReconnect()
        {
            Debug.Log("[DummyClientUnity] reconnect requested");
            State.ReconnectRequested = true;
            if (Flow != null)
            {
                Flow.Reconnect();
            }
        }
    }
}
