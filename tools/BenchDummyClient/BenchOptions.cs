using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;

namespace BenchDummyClient
{
    internal sealed class BenchOptions
    {
        public string AccountsCsv { get; set; } = string.Empty;
        public int AccountStartIndex { get; set; }
        public int SessionCount { get; set; } = 1;
        public int DurationSeconds { get; set; } = 300;
        public int MoveIntervalMs { get; set; } = 250;
        public int PortalIntervalSeconds { get; set; } = 20;
        public int ReconnectIntervalSeconds { get; set; }
        public int LaunchSpacingMs { get; set; } = 50;
        public int MaxConcurrentConnect { get; set; } = 25;
        public int ProcessShutdownGraceSeconds { get; set; } = 45;
        public string ResultDir { get; set; } = string.Empty;
        public string RunId { get; set; } = string.Empty;
        public string LoginHost { get; set; } = "127.0.0.1";
        public int LoginPort { get; set; } = 27780;
        public int RandomSeed { get; set; } = Environment.TickCount;
        public bool EnableMove { get; set; } = true;
        public bool EnablePortal { get; set; } = true;
        public bool EnableReconnect { get; set; } = true;
        public string ProcessLabel { get; set; } = string.Empty;

        public static BenchOptions Parse(string[] args)
        {
            var values = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            for (var i = 0; i < args.Length; i++)
            {
                var current = args[i] ?? string.Empty;
                if (!current.StartsWith("--", StringComparison.Ordinal))
                {
                    continue;
                }

                var key = current.Substring(2);
                string value;
                var eqIndex = key.IndexOf('=');
                if (eqIndex >= 0)
                {
                    value = key.Substring(eqIndex + 1);
                    key = key.Substring(0, eqIndex);
                }
                else if (i + 1 < args.Length && !args[i + 1].StartsWith("--", StringComparison.Ordinal))
                {
                    value = args[++i];
                }
                else
                {
                    value = "true";
                }
                values[key] = value;
            }

            var options = new BenchOptions
            {
                AccountsCsv = ParseString(values, "accounts-csv", string.Empty),
                AccountStartIndex = Math.Max(0, ParseInt(values, "account-start-index", 0)),
                SessionCount = Math.Max(1, ParseInt(values, "session-count", 1)),
                DurationSeconds = Math.Max(1, ParseInt(values, "duration-sec", 300)),
                MoveIntervalMs = Math.Max(100, ParseInt(values, "move-interval-ms", 250)),
                PortalIntervalSeconds = Math.Max(0, ParseInt(values, "portal-interval-sec", 20)),
                ReconnectIntervalSeconds = Math.Max(0, ParseInt(values, "reconnect-interval-sec", 0)),
                LaunchSpacingMs = Math.Max(0, ParseInt(values, "launch-spacing-ms", 50)),
                MaxConcurrentConnect = Math.Max(1, ParseInt(values, "max-concurrent-connect", 25)),
                ProcessShutdownGraceSeconds = Math.Max(5, ParseInt(values, "process-shutdown-grace-sec", 45)),
                ResultDir = ParseString(values, "result-dir", string.Empty),
                RunId = ParseString(values, "run-id", string.Empty),
                LoginHost = ParseString(values, "login-host", "127.0.0.1"),
                LoginPort = Math.Max(1, ParseInt(values, "login-port", 27780)),
                RandomSeed = ParseInt(values, "random-seed", Environment.TickCount),
                EnableMove = ParseBool(values, "enable-move", true),
                EnablePortal = ParseBool(values, "enable-portal", true),
                EnableReconnect = ParseBool(values, "enable-reconnect", true),
                ProcessLabel = ParseString(values, "process-label", string.Empty),
            };

            if (string.IsNullOrWhiteSpace(options.AccountsCsv))
            {
                throw new InvalidOperationException("--accounts-csv is required.");
            }
            options.AccountsCsv = Path.GetFullPath(options.AccountsCsv);
            if (!File.Exists(options.AccountsCsv))
            {
                throw new FileNotFoundException("accounts csv not found", options.AccountsCsv);
            }
            if (string.IsNullOrWhiteSpace(options.ResultDir))
            {
                options.ResultDir = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "bench_results");
            }
            options.ResultDir = Path.GetFullPath(options.ResultDir);
            if (string.IsNullOrWhiteSpace(options.RunId))
            {
                options.RunId = "bench_" + DateTime.Now.ToString("yyyy_MM_dd_HHmmss", CultureInfo.InvariantCulture);
            }
            return options;
        }

        private static bool ParseBool(IDictionary<string, string> values, string key, bool fallback)
        {
            if (!values.TryGetValue(key, out var raw))
            {
                return fallback;
            }
            if (string.IsNullOrWhiteSpace(raw))
            {
                return true;
            }
            if (bool.TryParse(raw, out var boolValue))
            {
                return boolValue;
            }
            if (int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out var intValue))
            {
                return intValue != 0;
            }
            return fallback;
        }

        private static int ParseInt(IDictionary<string, string> values, string key, int fallback)
        {
            if (!values.TryGetValue(key, out var raw))
            {
                return fallback;
            }
            return int.TryParse(raw, NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) ? value : fallback;
        }

        private static string ParseString(IDictionary<string, string> values, string key, string fallback)
        {
            return values.TryGetValue(key, out var raw) && !string.IsNullOrWhiteSpace(raw) ? raw : fallback;
        }
    }
}

