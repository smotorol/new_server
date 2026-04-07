using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using DummyClientWinForms.Models;
using DummyClientWinForms.Network;
using DummyClientWinForms.Services;

namespace BenchDummyClient
{
    internal sealed partial class BenchSession : IDisposable
    {
        private readonly object _sync = new object();
        private readonly BenchOptions _options;
        private readonly BenchAccountEntry _account;
        private readonly int _sessionIndex;
        private readonly BenchResultSink _sink;
        private readonly SemaphoreSlim _connectSemaphore;
        private readonly GameDataService _gameDataService;
        private readonly TcpClientEx _loginClient = new TcpClientEx();
        private readonly TcpClientEx _worldClient = new TcpClientEx();
        private readonly List<int> _portalRouteMaps = new List<int>();
        private readonly Random _random;
        private readonly BenchSessionResult _result;

        private DateTime _startedAtUtc;
        private DateTime _nextConnectAtUtc = DateTime.MinValue;
        private DateTime _nextLoginAtUtc = DateTime.MinValue;
        private DateTime _nextWorldSelectAtUtc = DateTime.MinValue;
        private DateTime _nextCharacterListAtUtc = DateTime.MinValue;
        private DateTime _nextCharacterSelectAtUtc = DateTime.MinValue;
        private DateTime _nextEnterAtUtc = DateTime.MinValue;
        private DateTime _nextMoveAtUtc = DateTime.MinValue;
        private DateTime _nextPortalAtUtc = DateTime.MinValue;
        private DateTime _nextReconnectAtUtc = DateTime.MaxValue;
        private bool _connectedToLogin;
        private bool _connectedToWorld;
        private bool _loggedIn;
        private bool _inWorld;
        private bool _enterWorldRequested;
        private bool _reconnectRequested;
        private bool _shutdownRequested;
        private bool _awaitingMoveResult;
        private bool _worldEnterCompleted;
        private bool _loginHandoffCompleted;
        private ulong _accountId;
        private ulong _selectedCharId;
        private ushort _selectedWorldId;
        private string _loginSession = string.Empty;
        private string _worldToken = string.Empty;
        private string _reconnectToken = string.Empty;
        private string _worldHost = string.Empty;
        private int _worldPort;
        private int _zoneId = 1;
        private int _mapId = 1;
        private int _posX;
        private int _posY;
        private readonly List<WorldSummary> _worlds = new List<WorldSummary>();
        private readonly List<CharacterSummary> _characters = new List<CharacterSummary>();
        private readonly List<PortalObject> _portals = new List<PortalObject>();

        public BenchSession(BenchOptions options, BenchAccountEntry account, int sessionIndex, BenchResultSink sink, SemaphoreSlim connectSemaphore, GameDataService gameDataService)
        {
            _options = options;
            _account = account;
            _sessionIndex = sessionIndex;
            _sink = sink;
            _connectSemaphore = connectSemaphore;
            _gameDataService = gameDataService;
            _random = new Random(options.RandomSeed + sessionIndex * 397);
            _result = new BenchSessionResult
            {
                run_id = options.RunId,
                process_label = options.ProcessLabel ?? string.Empty,
                process_id = System.Diagnostics.Process.GetCurrentProcess().Id,
                session_index = sessionIndex,
                client_tag = account.ClientTag ?? string.Empty,
                login_id = account.LoginId ?? string.Empty,
                final_status = BenchSessionStage.Created.ToString(),
                started_at_utc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
                updated_at_utc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
            };

            foreach (var token in (_account.PortalRoute ?? string.Empty).Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries))
            {
                if (int.TryParse(token, NumberStyles.Integer, CultureInfo.InvariantCulture, out var mapId) && mapId > 0)
                {
                    _portalRouteMaps.Add(mapId);
                }
            }

            _loginClient.PacketReceived += HandleLoginPacket;
            _worldClient.PacketReceived += HandleWorldPacket;
            _loginClient.ConnectionChanged += on => HandleConnectionChanged(false, on);
            _worldClient.ConnectionChanged += on => HandleConnectionChanged(true, on);
            _loginClient.Disconnected += (reason, expected) => HandleDisconnected(false, reason, expected);
            _worldClient.Disconnected += (reason, expected) => HandleDisconnected(true, reason, expected);
            _loginClient.Log += msg => _sink.Log($"session={_sessionIndex} [login] {msg}");
            _worldClient.Log += msg => _sink.Log($"session={_sessionIndex} [world] {msg}");
        }

        public async Task RunAsync(CancellationToken cancellationToken)
        {
            _startedAtUtc = DateTime.UtcNow;
            _nextMoveAtUtc = _startedAtUtc.AddMilliseconds(ComputeInitialDelayMs(_options.MoveIntervalMs, 100));
            _nextPortalAtUtc = _options.PortalIntervalSeconds > 0 && _options.EnablePortal
                ? _startedAtUtc.AddMilliseconds(ComputeInitialDelayMs(_options.PortalIntervalSeconds * 1000, 1000))
                : DateTime.MaxValue;
            _nextReconnectAtUtc = _options.ReconnectIntervalSeconds > 0 && _options.EnableReconnect
                ? _startedAtUtc.AddMilliseconds(ComputeInitialDelayMs(_options.ReconnectIntervalSeconds * 1000, 1000))
                : DateTime.MaxValue;
            RecordStatus(BenchSessionStage.Created, "session initialized");

            try
            {
                while (!_shutdownRequested && !cancellationToken.IsCancellationRequested)
                {
                    var now = DateTime.UtcNow;
                    if (now >= _startedAtUtc.AddSeconds(_options.DurationSeconds))
                    {
                        _result.timeout = true;
                        RecordStatus(BenchSessionStage.Completed, "duration reached");
                        FinalizeResult(_inWorld, _inWorld ? string.Empty : "timeout_before_enter_world");
                        break;
                    }

                    await TickAsync(now).ConfigureAwait(false);
                    await Task.Delay(100, cancellationToken).ConfigureAwait(false);
                }
            }
            catch (OperationCanceledException)
            {
                if (string.IsNullOrWhiteSpace(_result.ended_at_utc))
                {
                    FinalizeResult(_inWorld, _inWorld ? string.Empty : "canceled");
                }
            }
            catch (Exception ex)
            {
                RecordStatus(BenchSessionStage.Failed, ex.Message);
                FinalizeResult(false, ex.Message);
            }
            finally
            {
                await DisconnectAllAsync("session_end", true).ConfigureAwait(false);
                if (string.IsNullOrWhiteSpace(_result.ended_at_utc))
                {
                    FinalizeResult(_inWorld, _inWorld ? string.Empty : "session_closed");
                }
                _sink.RecordResult(_result);
            }
        }

        private async Task TickAsync(DateTime now)
        {
            if (!_connectedToLogin && !_loggedIn && !_reconnectRequested && now >= _nextConnectAtUtc)
            {
                _nextConnectAtUtc = now.AddSeconds(2);
                await ConnectLoginAsync().ConfigureAwait(false);
                return;
            }

            if (_connectedToLogin && !_loggedIn && string.IsNullOrWhiteSpace(_loginSession) && now >= _nextLoginAtUtc)
            {
                _nextLoginAtUtc = now.AddSeconds(2);
                RecordStatus(BenchSessionStage.LoginSent, "login request");
                await SendLoginPacketAsync(ClientProtocol.LoginRequest, ClientProtocol.BuildLoginRequest(_account.LoginId, _account.Password)).ConfigureAwait(false);
                return;
            }

            if (_loggedIn && _worlds.Count > 0 && _selectedWorldId == 0 && now >= _nextWorldSelectAtUtc)
            {
                _nextWorldSelectAtUtc = now.AddSeconds(2);
                var index = Math.Max(0, Math.Min(_account.WorldIndex, _worlds.Count - 1));
                var selected = _worlds[index];
                await SendLoginPacketAsync(ClientProtocol.WorldSelectRequest, ClientProtocol.BuildWorldSelectRequest(selected.WorldId, selected.ServerCode)).ConfigureAwait(false);
                return;
            }

            if (_selectedWorldId != 0 && _characters.Count == 0 && now >= _nextCharacterListAtUtc)
            {
                _nextCharacterListAtUtc = now.AddSeconds(2);
                await SendLoginPacketAsync(ClientProtocol.CharacterListRequest, ClientProtocol.BuildCharacterListRequest()).ConfigureAwait(false);
                return;
            }

            if (_selectedWorldId != 0 && _characters.Count > 0 && _selectedCharId == 0 && now >= _nextCharacterSelectAtUtc)
            {
                _nextCharacterSelectAtUtc = now.AddSeconds(2);
                var index = Math.Max(0, Math.Min(_account.CharacterIndex, _characters.Count - 1));
                var selected = _characters[index];
                await SendLoginPacketAsync(ClientProtocol.CharacterSelectRequest, ClientProtocol.BuildCharacterSelectRequest(selected.CharId)).ConfigureAwait(false);
                return;
            }

            if (CanIssueEnterWorld() && !_inWorld && !_enterWorldRequested && now >= _nextEnterAtUtc)
            {
                _nextEnterAtUtc = now.AddSeconds(2);
                await EnterWorldAsync().ConfigureAwait(false);
                return;
            }

            if (!_inWorld || _reconnectRequested || _enterWorldRequested)
            {
                return;
            }

            if (_options.EnableReconnect && _options.ReconnectIntervalSeconds > 0 && now >= _nextReconnectAtUtc)
            {
                _nextReconnectAtUtc = now.AddMilliseconds(ComputeRecurringDelayMs(_options.ReconnectIntervalSeconds * 1000, 1000));
                await ReconnectAsync().ConfigureAwait(false);
                return;
            }

            if (_awaitingMoveResult)
            {
                return;
            }

            if (_options.EnablePortal && _options.PortalIntervalSeconds > 0 && now >= _nextPortalAtUtc)
            {
                _nextPortalAtUtc = now.AddMilliseconds(ComputeRecurringDelayMs(_options.PortalIntervalSeconds * 1000, 1000));
                if (await TryPortalAsync().ConfigureAwait(false))
                {
                    return;
                }
            }

            if (_options.EnableMove && now >= _nextMoveAtUtc)
            {
                _nextMoveAtUtc = now.AddMilliseconds(ComputeRecurringDelayMs(_options.MoveIntervalMs, 100));
                await TryMoveAsync().ConfigureAwait(false);
            }
        }

        private async Task ConnectLoginAsync()
        {
            RecordStatus(BenchSessionStage.ConnectingLogin, "connect login");
            await _connectSemaphore.WaitAsync().ConfigureAwait(false);
            try
            {
                await _loginClient.ConnectAsync(_options.LoginHost, _options.LoginPort).ConfigureAwait(false);
            }
            finally
            {
                _connectSemaphore.Release();
            }
        }

        private async Task EnterWorldAsync()
        {
            if (!CanIssueEnterWorld())
            {
                _sink.Log($"session={_sessionIndex} enter_world deferred char_id={_selectedCharId} token_len={(_worldToken?.Length ?? 0)} host={_worldHost} port={_worldPort}");
                return;
            }
            if (!_worldClient.IsConnected)
            {
                RecordStatus(BenchSessionStage.ConnectingWorld, $"connect world {_worldHost}:{_worldPort}");
                await _connectSemaphore.WaitAsync().ConfigureAwait(false);
                try
                {
                    await _worldClient.ConnectAsync(_worldHost, _worldPort).ConfigureAwait(false);
                }
                finally
                {
                    _connectSemaphore.Release();
                }
            }
            _enterWorldRequested = true;
            RecordStatus(BenchSessionStage.EnterWorldPending, "enter world request");
            await SendWorldPacketAsync(ClientProtocol.EnterWorldWithToken, ClientProtocol.BuildEnterWorldRequest(_accountId, _loginSession, _worldToken)).ConfigureAwait(false);
        }
        private bool CanIssueEnterWorld()
        {
            return _selectedCharId != 0
                && !string.IsNullOrWhiteSpace(_worldToken)
                && _worldPort > 0
                && !string.IsNullOrWhiteSpace(_worldHost)
                && !string.IsNullOrWhiteSpace(_loginSession);
        }

        private async Task ReconnectAsync()
        {
            _reconnectRequested = true;
            _result.reconnect_attempts++;
            RecordStatus(BenchSessionStage.Reconnecting, "manual reconnect interval");
            await DisconnectAllAsync("manual_reconnect", true).ConfigureAwait(false);
            if (!string.IsNullOrWhiteSpace(_reconnectToken) && !string.IsNullOrWhiteSpace(_worldHost) && _worldPort > 0)
            {
                await _connectSemaphore.WaitAsync().ConfigureAwait(false);
                try
                {
                    await _worldClient.ConnectAsync(_worldHost, _worldPort).ConfigureAwait(false);
                }
                finally
                {
                    _connectSemaphore.Release();
                }
                await SendWorldPacketAsync(ClientProtocol.ReconnectWorld, ClientProtocol.BuildReconnectWorld(_accountId, _selectedCharId, _reconnectToken)).ConfigureAwait(false);
            }
            else
            {
                _reconnectRequested = false;
                await ConnectLoginAsync().ConfigureAwait(false);
            }
        }

        private async Task<bool> TryMoveAsync()
        {
            var angle = _random.NextDouble() * Math.PI * 2.0;
            var radius = 4 + _random.Next(30);
            var targetX = _posX + (int)Math.Round(Math.Cos(angle) * radius);
            var targetY = _posY + (int)Math.Round(Math.Sin(angle) * radius);
            _result.move_attempts++;
            _awaitingMoveResult = true;
            await SendWorldPacketAsync(ClientProtocol.Move, ClientProtocol.BuildMove(targetX, targetY)).ConfigureAwait(false);
            RecordStatus(BenchSessionStage.Moving, $"move target=({targetX},{targetY}) map={_mapId}");
            return true;
        }

        private async Task<bool> TryPortalAsync()
        {
            var portal = SelectPortal();
            if (portal == null)
            {
                return false;
            }
            _result.portal_attempts++;
            _awaitingMoveResult = true;
            await SendWorldPacketAsync(ClientProtocol.Move, ClientProtocol.BuildMove(portal.X, portal.Y)).ConfigureAwait(false);
            RecordStatus(BenchSessionStage.PortalRequested, $"portal target_map={ParsePortalDestMapId(portal)} pos=({portal.X},{portal.Y}) map={_mapId}");
            return true;
        }

        private PortalObject SelectPortal()
        {
            lock (_sync)
            {
                var candidates = _portals.Where(p => p.MapId == _mapId).ToList();
                if (candidates.Count == 0)
                {
                    return null;
                }
                var targetMap = ResolveNextPortalMap();
                if (targetMap > 0)
                {
                    var matched = candidates.Where(p => ParsePortalDestMapId(p) == targetMap).ToList();
                    if (matched.Count > 0)
                    {
                        candidates = matched;
                    }
                    else
                    {
                        _sink.Log("session=" + _sessionIndex + " portal skipped current_map=" + _mapId + " requested_target_map=" + targetMap + " reason=no_matching_portal");
                        return null;
                    }
                }

                var current = new Point(_posX, _posY);
                return candidates.OrderBy(p => DistanceSquared(p.ToPoint(), current)).FirstOrDefault();
            }
        }

        private int ResolveNextPortalMap()
        {
            if (_portalRouteMaps.Count == 0)
            {
                return 0;
            }
            var currentIndex = _portalRouteMaps.IndexOf(_mapId);
            if (currentIndex >= 0)
            {
                return _portalRouteMaps[(currentIndex + 1) % _portalRouteMaps.Count];
            }
            return _portalRouteMaps[0];
        }

        private void HandleConnectionChanged(bool isWorld, bool on)
        {
            lock (_sync)
            {
                if (isWorld) _connectedToWorld = on; else _connectedToLogin = on;
            }
        }

        private void HandleDisconnected(bool isWorld, string reason, bool expected)
        {
            lock (_sync)
            {
                if (isWorld) _connectedToWorld = false; else _connectedToLogin = false;
                _result.disconnected = true;
                _result.disconnect_count++;
            }
            if (!expected && !_shutdownRequested)
            {
                RecordStatus(BenchSessionStage.Failed, $"{(isWorld ? "world" : "login")}_disconnected {reason}");
                FinalizeResult(_inWorld, (isWorld ? "world" : "login") + "_disconnected");
                _shutdownRequested = true;
            }
        }

        private void HandleLoginPacket(ushort type, byte[] body)
        {
            switch (type)
            {
                case ClientProtocol.LoginResult:
                    var login = ClientProtocol.ParseLoginResult(body);
                    _loggedIn = login.Ok;
                    _accountId = login.AccountId;
                    _loginSession = login.LoginSession ?? string.Empty;
                    _result.account_id = login.AccountId;
                    _result.login_success = login.Ok;
                    RecordStatus(login.Ok ? BenchSessionStage.LoginSuccess : BenchSessionStage.Failed, "login_result=" + login.FailReason);
                    if (!login.Ok)
                    {
                        FinalizeResult(false, string.IsNullOrWhiteSpace(login.FailReason) ? "login_failed" : login.FailReason);
                        _shutdownRequested = true;
                    }
                    else
                    {
                        _ = SendLoginPacketAsync(ClientProtocol.WorldListRequest, ClientProtocol.BuildWorldListRequest());
                    }
                    break;
                case ClientProtocol.WorldListResponse:
                    bool worldsOk;
                    string worldsReason;
                    var worlds = ClientProtocol.ParseWorldList(body, out worldsOk, out worldsReason);
                    lock (_sync)
                    {
                        _worlds.Clear();
                        _worlds.AddRange(worlds);
                    }
                    if (!worldsOk)
                    {
                        RecordStatus(BenchSessionStage.Failed, "world_list_failed " + worldsReason);
                        FinalizeResult(false, string.IsNullOrWhiteSpace(worldsReason) ? "world_list_failed" : worldsReason);
                        _shutdownRequested = true;
                    }
                    break;
                case ClientProtocol.WorldSelectResponse:
                    var worldSelect = ClientProtocol.ParseWorldSelect(body);
                    if (!worldSelect.Ok)
                    {
                        RecordStatus(BenchSessionStage.Failed, "world_select_failed " + worldSelect.FailReason);
                        FinalizeResult(false, string.IsNullOrWhiteSpace(worldSelect.FailReason) ? "world_select_failed" : worldSelect.FailReason);
                        _shutdownRequested = true;
                        break;
                    }
                    _selectedWorldId = worldSelect.WorldId;
                    _worldHost = worldSelect.WorldHost;
                    _worldPort = worldSelect.WorldPort;
                    _result.world_selected = true;
                    RecordStatus(BenchSessionStage.WorldSelected, $"world_id={worldSelect.WorldId}");
                    break;
                case ClientProtocol.CharacterListResponse:
                    bool charsOk;
                    string charsReason;
                    var chars = ClientProtocol.ParseCharacterList(body, out charsOk, out charsReason);
                    lock (_sync)
                    {
                        _characters.Clear();
                        _characters.AddRange(chars);
                    }
                    if (!charsOk || chars.Count == 0)
                    {
                        RecordStatus(BenchSessionStage.Failed, charsOk ? "character_list_empty" : "character_list_failed " + charsReason);
                        FinalizeResult(false, charsOk ? "character_list_empty" : (string.IsNullOrWhiteSpace(charsReason) ? "character_list_failed" : charsReason));
                        _shutdownRequested = true;
                        break;
                    }
                    RecordStatus(BenchSessionStage.CharacterListReceived, "count=" + chars.Count.ToString(CultureInfo.InvariantCulture));
                    break;
                case ClientProtocol.CharacterSelectResponse:
                    var charSelect = ClientProtocol.ParseCharacterSelect(body);
                    if (!charSelect.Ok)
                    {
                        RecordStatus(BenchSessionStage.Failed, "character_select_failed " + charSelect.FailReason);
                        FinalizeResult(false, string.IsNullOrWhiteSpace(charSelect.FailReason) ? "character_select_failed" : charSelect.FailReason);
                        _shutdownRequested = true;
                        break;
                    }
                    _selectedCharId = charSelect.CharId;
                    _worldHost = charSelect.WorldHost;
                    _worldPort = charSelect.WorldPort;
                    _worldToken = charSelect.WorldToken ?? string.Empty;
                    if (_selectedCharId == 0 || string.IsNullOrWhiteSpace(_worldToken) || string.IsNullOrWhiteSpace(_worldHost) || _worldPort <= 0)
                    {
                        var reason = $"character_select_incomplete_context char_id={_selectedCharId} token_len={_worldToken.Length} host={_worldHost} port={_worldPort}";
                        RecordStatus(BenchSessionStage.Failed, reason);
                        FinalizeResult(false, "character_select_incomplete_context");
                        _shutdownRequested = true;
                        break;
                    }
                    _result.char_id = charSelect.CharId;
                    _result.character_selected = true;
                    RecordStatus(BenchSessionStage.CharacterSelected, "char_id=" + charSelect.CharId.ToString(CultureInfo.InvariantCulture));
                    if (!_loginHandoffCompleted)
                    {
                        _loginHandoffCompleted = true;
                        _ = DisconnectLoginAfterHandoffAsync();
                    }
                    break;
            }
            }

        private void HandleWorldPacket(ushort type, byte[] body)
        {
            switch (type)
            {
                case ClientProtocol.EnterWorldResult:
                    var enter = ClientProtocol.ParseEnterWorldResult(body);
                    _enterWorldRequested = false;
                    if (!enter.Ok)
                    {
                        RecordStatus(BenchSessionStage.Failed, "enter_world_failed reason=" + enter.Reason.ToString(CultureInfo.InvariantCulture));
                        FinalizeResult(false, "enter_world_failed");
                        _shutdownRequested = true;
                        break;
                    }
                    _reconnectToken = enter.ReconnectToken ?? string.Empty;
                    _worldEnterCompleted = true;
                    _result.enter_world_success = true;
                    ResetActivityScheduleAfterWorldReady();
                    RecordStatus(BenchSessionStage.EnterWorldSuccess, "enter world ok");
                    break;
                case ClientProtocol.ReconnectWorldResult:
                    var reconnect = ClientProtocol.ParseReconnectWorldResult(body);
                    if (!reconnect.Ok)
                    {
                        RecordStatus(BenchSessionStage.Failed, "reconnect_failed reason=" + reconnect.Reason.ToString(CultureInfo.InvariantCulture));
                        FinalizeResult(false, "reconnect_failed");
                        _shutdownRequested = true;
                        break;
                    }
                    _reconnectRequested = false;
                    _result.reconnect_success = true;
                    _reconnectToken = reconnect.ReconnectToken ?? string.Empty;
                    ApplyZoneState((int)reconnect.ZoneId, (int)reconnect.MapId, reconnect.X, reconnect.Y);
                    ResetActivityScheduleAfterWorldReady();
                    RecordStatus(BenchSessionStage.EnterWorldSuccess, "reconnect ok");
                    break;
                case ClientProtocol.ZoneMapState:
                    var zoneState = ClientProtocol.ParseZoneMapState(body);
                    ApplyZoneState((int)zoneState.ZoneId, (int)zoneState.MapId, zoneState.X, zoneState.Y);
                    break;
                case ClientProtocol.PlayerMove:
                    var move = ClientProtocol.ParsePlayerMove(body, _selectedCharId);
                    if (move != null && move.IsSelf) UpdateSelfPosition(move.X, move.Y);
                    break;
                case ClientProtocol.PlayerMoveBatch:
                    var moveBatch = ClientProtocol.ParsePlayerMoveBatch(body, _selectedCharId);
                    var selfMove = moveBatch.FirstOrDefault(x => x.IsSelf);
                    if (selfMove != null) UpdateSelfPosition(selfMove.X, selfMove.Y);
                    break;
            }
        }

        private void ApplyZoneState(int zoneId, int mapId, int x, int y)
        {
            lock (_sync)
            {
                _zoneId = zoneId;
                _mapId = mapId;
                _posX = x;
                _posY = y;
                _inWorld = true;
                _awaitingMoveResult = false;
                _result.zone_id = zoneId;
                _result.map_id = mapId;
                _result.pos_x = x;
                _result.pos_y = y;
            }
            RefreshPortalsForZone(zoneId);
        }

        private void UpdateSelfPosition(int x, int y)
        {
            lock (_sync)
            {
                _posX = x;
                _posY = y;
                _result.pos_x = x;
                _result.pos_y = y;
                _awaitingMoveResult = false;
            }
        }

        private void RefreshPortalsForZone(int zoneId)
        {
            var overlay = _gameDataService.LoadZoneOverlay(zoneId);
            lock (_sync)
            {
                _portals.Clear();
                _portals.AddRange(overlay.OfType<PortalObject>());
            }
        }

        private async Task SendLoginPacketAsync(ushort type, byte[] body)
        {
            if (_loginClient.IsConnected)
            {
                await _loginClient.SendAsync(type, body).ConfigureAwait(false);
            }
        }

        private async Task SendWorldPacketAsync(ushort type, byte[] body)
        {
            if (_worldClient.IsConnected)
            {
                await _worldClient.SendAsync(type, body).ConfigureAwait(false);
            }
        }

        private async Task DisconnectAllAsync(string reason, bool expected)
        {
            try { await _worldClient.DisconnectAsync(reason, expected).ConfigureAwait(false); } catch { }
            try { await _loginClient.DisconnectAsync(reason, expected).ConfigureAwait(false); } catch { }
        }

        private async Task DisconnectLoginAfterHandoffAsync()
        {
            try
            {
                await _loginClient.DisconnectAsync("login_handoff_complete", true).ConfigureAwait(false);
            }
            catch (Exception ex)
            {
                _sink.Log($"session={_sessionIndex} login handoff disconnect failed: {ex.Message}");
            }
        }

        private static int ParsePortalDestMapId(PortalObject portal)
        {
            if (portal == null || string.IsNullOrWhiteSpace(portal.Label)) return 0;
            var label = portal.Label;
            var slashIndex = label.LastIndexOf('/');
            if (slashIndex < 0 || slashIndex + 1 >= label.Length) return 0;
            return int.TryParse(label.Substring(slashIndex + 1), NumberStyles.Integer, CultureInfo.InvariantCulture, out var value) ? value : 0;
        }

        private static int DistanceSquared(Point a, Point b)
        {
            var dx = a.X - b.X;
            var dy = a.Y - b.Y;
            return dx * dx + dy * dy;
        }

        private void RecordStatus(BenchSessionStage stage, string detail)
        {
            lock (_sync)
            {
                _result.final_status = stage.ToString();
                _result.updated_at_utc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
                _result.events.Add(new BenchSessionEvent
                {
                    timestamp_utc = _result.updated_at_utc,
                    status = stage.ToString(),
                    detail = detail ?? string.Empty,
                });
                if (_result.events.Count > 64) _result.events.RemoveAt(0);
            }
            _sink.Log($"session={_sessionIndex} status={stage} detail={detail}");
        }

        private void FinalizeResult(bool success, string failureReason)
        {
            lock (_sync)
            {
                if (!string.IsNullOrWhiteSpace(_result.ended_at_utc)) return;
                _result.success = success;
                _result.failure_reason = failureReason ?? string.Empty;
                _result.ended_at_utc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
                _result.updated_at_utc = _result.ended_at_utc;
                if (!success) _result.final_status = BenchSessionStage.Failed.ToString();
            }
        }

        public void Dispose()
        {
            _loginClient.Dispose();
            _worldClient.Dispose();
        }
    }
}











