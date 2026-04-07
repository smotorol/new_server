using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using DummyClientWinForms.Models;
using DummyClientWinForms.Network;
using DummyClientWinForms.Services;

namespace DummyClientWinForms
{
    public partial class MainForm
    {
        private readonly Timer _automationTimer = new Timer { Interval = 100 };
        private readonly List<int> _automationPortalRouteMaps = new List<int>();
        private Random _automationRandom;
        private StreamWriter _automationLogWriter;
        private DateTime _automationStartedAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationConnectAttemptAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationLoginAttemptAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationWorldSelectAttemptAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationCharacterListAttemptAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationCharacterSelectAttemptAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationEnterAttemptAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationMoveAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationPortalAtUtc = DateTime.MinValue;
        private DateTime _nextAutomationReconnectAtUtc = DateTime.MinValue;
        private bool _automationTickBusy;
        private bool _automationStarted;
        private bool _automationShutdownRequested;

        private bool IsAutomationEnabled => _automationOptions != null && _automationOptions.Enabled;

        private void InitializeAutomation()
        {
            if (!IsAutomationEnabled)
            {
                return;
            }

            _automationRandom = new Random(_automationOptions.RandomSeed == 0
                ? Environment.TickCount ^ (_automationOptions.ClientIndex * 397)
                : _automationOptions.RandomSeed + _automationOptions.ClientIndex);
            _automationPortalRouteMaps.Clear();
            foreach (var token in (_automationOptions.PortalRoute ?? string.Empty)
                .Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries))
            {
                int mapId;
                if (int.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out mapId) && mapId > 0)
                {
                    _automationPortalRouteMaps.Add(mapId);
                }
            }

            _loginPanel.HostTextBox.Text = _automationOptions.LoginHost;
            _loginPanel.PortTextBox.Text = _automationOptions.LoginPort.ToString(CultureInfo.InvariantCulture);
            _loginPanel.LoginIdTextBox.Text = _automationOptions.LoginId;
            _loginPanel.PasswordTextBox.Text = _automationOptions.Password;

            if (_automationOptions.Headless)
            {
                ShowInTaskbar = false;
                WindowState = FormWindowState.Minimized;
                _state.GeometryOverlayEnabled = false;
                _state.PortalMarkersEnabled = false;
                _state.SafeZoneMarkersEnabled = false;
                _state.SpawnMarkersEnabled = false;
                _state.MiniMapEnabled = false;
                _state.MiniMapLegendEnabled = false;
                _worldPanel.GeometryOverlayCheckBox.Checked = false;
                _worldPanel.PortalMarkersCheckBox.Checked = false;
                _worldPanel.SafeZoneMarkersCheckBox.Checked = false;
                _worldPanel.SpawnMarkersCheckBox.Checked = false;
                _worldPanel.MiniMapCheckBox.Checked = false;
                _worldPanel.MiniMapLegendCheckBox.Checked = false;
            }

            if (!string.IsNullOrWhiteSpace(_automationOptions.ClientLogPath))
            {
                Directory.CreateDirectory(Path.GetDirectoryName(_automationOptions.ClientLogPath) ?? AppDomain.CurrentDomain.BaseDirectory);
                _automationLogWriter = new StreamWriter(new FileStream(_automationOptions.ClientLogPath, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
                {
                    AutoFlush = true
                };
            }

            InitializeAutomationResultTracking();

            _automationTimer.Tick += async (s, e) => await HandleAutomationTickAsync();
            Shown += async (s, e) => await StartAutomationAsync();
            FormClosed += (s, e) =>
            {
                _automationTimer.Stop();
                if (!_automationShutdownRequested)
                {
                    AutomationMarkProcessClosed();
                }
                _automationLogWriter?.Flush();
                _automationLogWriter?.Dispose();
                _automationLogWriter = null;
            };

            AppendAutomationTrace($"automation configured client_index={_automationOptions.ClientIndex} login_id={_automationOptions.LoginId} duration_sec={_automationOptions.DurationSeconds} move_interval_ms={_automationOptions.MoveIntervalMs} portal_interval_sec={_automationOptions.PortalIntervalSeconds} reconnect_interval_sec={_automationOptions.ReconnectIntervalSeconds} route={string.Join("/", _automationPortalRouteMaps)} headless={(_automationOptions.Headless ? 1 : 0)}");
            RecordAutomationStatus("configured", "automation initialized");
        }

        private async Task StartAutomationAsync()
        {
            if (!IsAutomationEnabled || _automationStarted)
            {
                return;
            }

            _automationStarted = true;
            _automationStartedAtUtc = DateTime.UtcNow;
            _nextAutomationConnectAttemptAtUtc = DateTime.MinValue;
            _nextAutomationLoginAttemptAtUtc = DateTime.MinValue;
            _nextAutomationWorldSelectAttemptAtUtc = DateTime.MinValue;
            _nextAutomationCharacterListAttemptAtUtc = DateTime.MinValue;
            _nextAutomationCharacterSelectAttemptAtUtc = DateTime.MinValue;
            _nextAutomationEnterAttemptAtUtc = DateTime.MinValue;
            _nextAutomationMoveAtUtc = DateTime.UtcNow.AddMilliseconds(_automationOptions.MoveIntervalMs);
            _nextAutomationPortalAtUtc = DateTime.UtcNow.AddSeconds(Math.Max(1, _automationOptions.PortalIntervalSeconds));
            _nextAutomationReconnectAtUtc = _automationOptions.ReconnectIntervalSeconds > 0
                ? DateTime.UtcNow.AddSeconds(_automationOptions.ReconnectIntervalSeconds)
                : DateTime.MaxValue;
            _automationTimer.Start();
            RecordAutomationStatus("launched", "automation timer started");

            if (_automationOptions.Headless)
            {
                BeginInvoke((Action)(() => Hide()));
            }

            await EnsureAutomationProgressAsync();
        }

        private async Task HandleAutomationTickAsync()
        {
            if (!IsAutomationEnabled || _automationTickBusy)
            {
                return;
            }

            _automationTickBusy = true;
            try
            {
                await EnsureAutomationProgressAsync();
            }
            finally
            {
                _automationTickBusy = false;
            }
        }

        private async Task EnsureAutomationProgressAsync()
        {
            if (!IsAutomationEnabled || _automationShutdownRequested)
            {
                return;
            }

            var now = DateTime.UtcNow;
            if (_automationStartedAtUtc != DateTime.MinValue && now >= _automationStartedAtUtc.AddSeconds(_automationOptions.DurationSeconds))
            {
                AutomationMarkTimeoutReached();
                await ShutdownAutomationAsync();
                return;
            }

            if (!_state.ConnectedToLogin && !_state.LoggedIn && !_state.ReconnectRequested && now >= _nextAutomationConnectAttemptAtUtc)
            {
                _nextAutomationConnectAttemptAtUtc = now.AddSeconds(2);
                AppendAutomationTrace("automation connect login");
                await ConnectLoginAsync();
                return;
            }

            if (_state.ConnectedToLogin && !_state.LoggedIn && string.IsNullOrWhiteSpace(_state.LoginSession) && now >= _nextAutomationLoginAttemptAtUtc)
            {
                _nextAutomationLoginAttemptAtUtc = now.AddSeconds(2);
                AppendAutomationTrace("automation login request");
                await LoginAsync();
                return;
            }

            if (_state.LoggedIn && _state.Worlds.Count > 0 && _state.SelectedWorldId == 0 && now >= _nextAutomationWorldSelectAttemptAtUtc)
            {
                _nextAutomationWorldSelectAttemptAtUtc = now.AddSeconds(2);
                if (_state.Worlds.Count > 0)
                {
                    var index = Math.Max(0, Math.Min(_automationOptions.WorldIndex, _state.Worlds.Count - 1));
                    _characterPanel.WorldListBox.SelectedItem = _state.Worlds[index];
                    AppendAutomationTrace($"automation world select index={index} world_id={_state.Worlds[index].WorldId}");
                    await SelectWorldAsync();
                    return;
                }
            }

            if (_state.CurrentStage == ClientStage.WorldSelected && _state.Characters.Count == 0 && now >= _nextAutomationCharacterListAttemptAtUtc)
            {
                _nextAutomationCharacterListAttemptAtUtc = now.AddSeconds(2);
                AppendAutomationTrace("automation character list request");
                await SendLoginPacketAsync(ClientProtocol.CharacterListRequest, ClientProtocol.BuildCharacterListRequest());
                return;
            }

            if (_state.CurrentStage >= ClientStage.WorldSelected && _state.Characters.Count > 0 && _state.SelectedCharId == 0 && now >= _nextAutomationCharacterSelectAttemptAtUtc)
            {
                _nextAutomationCharacterSelectAttemptAtUtc = now.AddSeconds(2);
                var index = Math.Max(0, Math.Min(_automationOptions.CharacterIndex, _state.Characters.Count - 1));
                _characterPanel.CharacterListBox.SelectedItem = _state.Characters[index];
                AppendAutomationTrace($"automation character select index={index} char_id={_state.Characters[index].CharId}");
                await SelectCharacterAsync();
                return;
            }

            if (_state.CurrentStage == ClientStage.CharacterSelected && _state.SelectedCharId != 0 && !string.IsNullOrWhiteSpace(_state.WorldToken) && !string.IsNullOrWhiteSpace(_state.WorldHost) && _state.WorldPort > 0 && !_state.InWorld && !_state.EnterWorldRequested && now >= _nextAutomationEnterAttemptAtUtc)
            {
                _nextAutomationEnterAttemptAtUtc = now.AddSeconds(2);
                AppendAutomationTrace("automation enter world");
                await EnterWorldAsync();
                return;
            }

            if (!_state.InWorld || _state.CurrentStage != ClientStage.InWorld || _state.ReconnectRequested || _state.EnterWorldRequested)
            {
                return;
            }

            if (_automationOptions.ReconnectIntervalSeconds > 0 && now >= _nextAutomationReconnectAtUtc)
            {
                _nextAutomationReconnectAtUtc = now.AddSeconds(_automationOptions.ReconnectIntervalSeconds);
                AppendAutomationTrace("automation reconnect");
                AutomationMarkReconnectRequested();
                await ReconnectAsync();
                return;
            }

            if (_state.AwaitingAuthoritativeMoveResult)
            {
                return;
            }

            if (_automationOptions.PortalIntervalSeconds > 0 && now >= _nextAutomationPortalAtUtc)
            {
                _nextAutomationPortalAtUtc = now.AddSeconds(_automationOptions.PortalIntervalSeconds);
                if (await TryRunAutomationPortalAsync())
                {
                    return;
                }
            }

            if (now >= _nextAutomationMoveAtUtc)
            {
                _nextAutomationMoveAtUtc = now.AddMilliseconds(_automationOptions.MoveIntervalMs);
                await TryRunAutomationMoveAsync();
            }
        }

        private async Task<bool> TryRunAutomationMoveAsync()
        {
            var originX = _state.PosX;
            var originY = _state.PosY;
            for (var attempt = 0; attempt < 12; attempt++)
            {
                var angle = _automationRandom.NextDouble() * Math.PI * 2.0;
                var distance = 4 + _automationRandom.Next(Math.Max(4, _automationOptions.WanderRadius));
                var targetX = originX + (int)Math.Round(Math.Cos(angle) * distance);
                var targetY = originY + (int)Math.Round(Math.Sin(angle) * distance);
                string blockedReason;
                if (!TryValidateMovementAgainstGeometry(targetX, targetY, "AutoWander", false, out blockedReason))
                {
                    continue;
                }

                if (await RequestMoveToAsync(targetX, targetY, "AutoWander", logToConsole: false))
                {
                    AppendAutomationTrace($"automation move target=({targetX},{targetY}) map={_state.MapId}");
                    return true;
                }
            }

            AppendAutomationTrace($"automation move skipped map={_state.MapId} reason=no_walkable_target");
            return false;
        }

        private async Task<bool> TryRunAutomationPortalAsync()
        {
            var portal = SelectAutomationPortal();
            if (portal == null)
            {
                AppendAutomationTrace($"automation portal skipped map={_state.MapId} reason=no_portal");
                return false;
            }

            if (await RequestMoveToAsync(portal.X, portal.Y, "AutoPortal", logToConsole: true))
            {
                AppendAutomationTrace($"automation portal target_map={ParsePortalDestMapId(portal)} pos=({portal.X},{portal.Y}) current_map={_state.MapId}");
                return true;
            }
            return false;
        }

        private PortalObject SelectAutomationPortal()
        {
            var portals = _state.StaticObjects.OfType<PortalObject>().Where(p => p.MapId == _state.MapId).ToList();
            if (portals.Count == 0)
            {
                return null;
            }

            var targetMap = ResolveAutomationNextPortalMap();
            IEnumerable<PortalObject> candidates = portals;
            if (targetMap > 0)
            {
                var matched = portals.Where(p => ParsePortalDestMapId(p) == targetMap).ToList();
                if (matched.Count > 0)
                {
                    candidates = matched;
                }
                else
                {
                    AppendAutomationTrace("automation portal skipped map=" + _state.MapId + " requested_target_map=" + targetMap + " reason=no_matching_portal");
                    return null;
                }
            }

            var current = new System.Drawing.Point(_state.PosX, _state.PosY);
            return candidates.OrderBy(p => DistanceSquared(p.ToPoint(), current)).FirstOrDefault();
        }

        private int ResolveAutomationNextPortalMap()
        {
            if (_automationPortalRouteMaps.Count == 0)
            {
                return 0;
            }

            var currentIndex = _automationPortalRouteMaps.IndexOf(_state.MapId);
            if (currentIndex >= 0)
            {
                return _automationPortalRouteMaps[(currentIndex + 1) % _automationPortalRouteMaps.Count];
            }
            return _automationPortalRouteMaps[0];
        }

        private static int ParsePortalDestMapId(PortalObject portal)
        {
            if (portal == null || string.IsNullOrWhiteSpace(portal.Label))
            {
                return 0;
            }
            var label = portal.Label;
            var slashIndex = label.LastIndexOf('/');
            if (slashIndex < 0 || slashIndex + 1 >= label.Length)
            {
                return 0;
            }
            int value;
            return int.TryParse(label.Substring(slashIndex + 1), NumberStyles.Integer, CultureInfo.InvariantCulture, out value) ? value : 0;
        }

        private async Task ShutdownAutomationAsync()
        {
            if (_automationShutdownRequested)
            {
                return;
            }

            _automationShutdownRequested = true;
            _automationTimer.Stop();
            AppendAutomationTrace("automation shutdown requested");
            AutomationMarkCompleted();
            await DisconnectAllAsync(DisconnectReason.FormClosing);
            BeginInvoke((Action)(() => Close()));
        }

        private void AppendAutomationTrace(string message)
        {
            AppendLog($"[auto] {message}");
            if (_automationLogWriter != null)
            {
                _automationLogWriter.WriteLine($"[{DateTime.Now:yyyy-MM-dd HH:mm:ss}] {message}");
            }
        }
    }
}





