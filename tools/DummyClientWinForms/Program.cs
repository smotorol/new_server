using System;
using System.Collections.Generic;
using System.Globalization;
using System.Windows.Forms;

namespace DummyClientWinForms
{
    public sealed class AutoClientOptions
    {
        public bool Enabled { get; set; }
        public bool Headless { get; set; }
        public string LoginHost { get; set; } = "127.0.0.1";
        public int LoginPort { get; set; } = 27780;
        public string LoginId { get; set; } = "test1";
        public string Password { get; set; } = "pw1";
        public int WorldIndex { get; set; }
        public int CharacterIndex { get; set; }
        public int DurationSeconds { get; set; } = 300;
        public int MoveIntervalMs { get; set; } = 250;
        public int PortalIntervalSeconds { get; set; } = 20;
        public int ReconnectIntervalSeconds { get; set; }
        public int RandomSeed { get; set; } = Environment.TickCount;
        public int WanderRadius { get; set; } = 30;
        public string PortalRoute { get; set; } = "2,4,7,1";
        public string ClientLogPath { get; set; } = string.Empty;
        public string ClientResultPath { get; set; } = string.Empty;
        public string RunId { get; set; } = string.Empty;
        public int ClientIndex { get; set; }
        public string ClientTag { get; set; } = string.Empty;
        public bool AutoStartServers { get; set; }
    }

    internal static class Program
    {
        [STAThread]
        private static void Main(string[] args)
        {
            var options = ParseOptions(args);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.Run(new MainForm(options));
        }

        private static AutoClientOptions ParseOptions(string[] args)
        {
            var options = new AutoClientOptions();
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

            options.Enabled = ParseBool(values, "auto", false);
            options.Headless = ParseBool(values, "headless", false);
            options.LoginHost = ParseString(values, "login-host", options.LoginHost);
            options.LoginPort = ParseInt(values, "login-port", options.LoginPort);
            options.LoginId = ParseString(values, "login-id", options.LoginId);
            options.Password = ParseString(values, "password", options.Password);
            options.WorldIndex = ParseInt(values, "world-index", 0);
            options.CharacterIndex = ParseInt(values, "character-index", 0);
            options.DurationSeconds = Math.Max(1, ParseInt(values, "duration-sec", options.DurationSeconds));
            options.MoveIntervalMs = Math.Max(100, ParseInt(values, "move-interval-ms", options.MoveIntervalMs));
            options.PortalIntervalSeconds = Math.Max(0, ParseInt(values, "portal-interval-sec", options.PortalIntervalSeconds));
            options.ReconnectIntervalSeconds = Math.Max(0, ParseInt(values, "reconnect-interval-sec", options.ReconnectIntervalSeconds));
            options.RandomSeed = ParseInt(values, "random-seed", options.RandomSeed);
            options.WanderRadius = Math.Max(5, ParseInt(values, "wander-radius", options.WanderRadius));
            options.PortalRoute = ParseString(values, "portal-route", options.PortalRoute);
            options.ClientLogPath = ParseString(values, "client-log-path", options.ClientLogPath);
            options.ClientResultPath = ParseString(values, "client-result-path", options.ClientResultPath);
            options.RunId = ParseString(values, "run-id", options.RunId);
            options.ClientIndex = Math.Max(0, ParseInt(values, "client-index", 0));
            options.ClientTag = ParseString(values, "client-tag", options.ClientTag);
            options.AutoStartServers = ParseBool(values, "auto-start-servers", false);
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
