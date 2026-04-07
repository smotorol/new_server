using System;

namespace BenchDummyClient
{
    internal sealed partial class BenchSession
    {
        private void ResetActivityScheduleAfterWorldReady()
        {
            var now = DateTime.UtcNow;
            lock (_sync)
            {
                _nextMoveAtUtc = now.AddMilliseconds(ComputeRecurringDelayMsLocked_(Math.Max(100, _options.MoveIntervalMs), 100));
                _nextPortalAtUtc = _options.PortalIntervalSeconds > 0 && _options.EnablePortal
                    ? now.AddMilliseconds(ComputeRecurringDelayMsLocked_(Math.Max(1000, _options.PortalIntervalSeconds * 1000), 1000))
                    : DateTime.MaxValue;
                _nextReconnectAtUtc = _options.ReconnectIntervalSeconds > 0 && _options.EnableReconnect
                    ? now.AddMilliseconds(ComputeRecurringDelayMsLocked_(Math.Max(1000, _options.ReconnectIntervalSeconds * 1000), 1000))
                    : DateTime.MaxValue;
            }
        }

        private int ComputeInitialDelayMs(int baseMs, int floorMs)
        {
            lock (_sync)
            {
                return ComputeInitialDelayMsLocked_(baseMs, floorMs);
            }
        }

        private int ComputeRecurringDelayMs(int baseMs, int floorMs)
        {
            lock (_sync)
            {
                return ComputeRecurringDelayMsLocked_(baseMs, floorMs);
            }
        }

        private int ComputeInitialDelayMsLocked_(int baseMs, int floorMs)
        {
            if (baseMs <= 0)
            {
                return floorMs;
            }

            var jitterWindow = Math.Max(floorMs, baseMs / 2);
            return Math.Max(floorMs, baseMs + _random.Next(0, jitterWindow + 1));
        }

        private int ComputeRecurringDelayMsLocked_(int baseMs, int floorMs)
        {
            if (baseMs <= 0)
            {
                return floorMs;
            }

            var jitterWindow = Math.Max(floorMs, baseMs / 4);
            return Math.Max(floorMs, baseMs + _random.Next(-jitterWindow, jitterWindow + 1));
        }
    }
}
