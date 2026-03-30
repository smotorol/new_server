using System;
using System.Drawing;
using System.Linq;
using System.Threading.Tasks;
using System.Windows.Forms;
using DummyClientWinForms.Models;
using DummyClientWinForms.Network;
using DummyClientWinForms.Services;

namespace DummyClientWinForms
{
    public partial class MainForm : Form
    {
        private readonly DummyClientState _state = new DummyClientState();
        private readonly TcpClientEx _loginClient = new TcpClientEx();
        private readonly TcpClientEx _worldClient = new TcpClientEx();
        private readonly WorldRenderService _renderService = new WorldRenderService();
        private readonly GameDataService _gameDataService = new GameDataService();
        private string _selectedObjectId = string.Empty;
        private ulong _lastMonsterId;

        public MainForm()
        {
            InitializeComponent();
            WireEvents();
            _worldPanel.AutoOverlayCheckBox.Checked = true;
            LoadZoneOverlay(1, false);
            RefreshUi();
        }

        private void WireEvents()
        {
            _loginPanel.ConnectRequested += async () => await ConnectLoginAsync();
            _loginPanel.LoginRequested += async () => await LoginAsync();
            _characterPanel.LoadWorldsRequested += async () => await SendLoginPacketAsync(ClientProtocol.WorldListRequest, ClientProtocol.BuildWorldListRequest());
            _characterPanel.SelectWorldRequested += async () => await SelectWorldAsync();
            _characterPanel.LoadCharactersRequested += async () => await SendLoginPacketAsync(ClientProtocol.CharacterListRequest, ClientProtocol.BuildCharacterListRequest());
            _characterPanel.SelectCharacterRequested += async () => await SelectCharacterAsync();
            _characterPanel.EnterWorldRequested += async () => await EnterWorldAsync();
            _characterPanel.StatsRequested += async () => await SendWorldPacketAsync(ClientProtocol.GetStats, ClientProtocol.BuildGetStats());
            _characterPanel.HealRequested += async () => await SendWorldPacketAsync(ClientProtocol.HealSelf, ClientProtocol.BuildHeal(0));
            _characterPanel.GoldRequested += async () => await SendWorldPacketAsync(ClientProtocol.AddGold, ClientProtocol.BuildGold(100));
            _characterPanel.MoveRequested += async () => await MoveAsync();
            _characterPanel.AttackRequested += async () => await AttackAsync();
            _characterPanel.ReconnectRequested += async () => await ReconnectAsync();
            _characterPanel.SoftLogoutRequested += async () => await SoftLogoutAsync();
            _characterPanel.LogoutRequested += async () => await LogoutAsync();
            _worldPanel.ZoneOverlayChanged += zone => { if (!_state.AutoOverlayZone) LoadZoneOverlay(zone, true); };
            _worldPanel.AutoOverlayChanged += enabled =>
            {
                _state.AutoOverlayZone = enabled;
                if (enabled)
                {
                    ApplyOverlaySelectionFromState();
                }
                RefreshUi();
            };
            _worldPanel.Canvas.Paint += (s, e) => _renderService.Render(e.Graphics, _worldPanel.Canvas.ClientRectangle, _state, _selectedObjectId);
            _worldPanel.Canvas.MouseClick += (s, e) => SelectNearestObject(e.Location);

            _loginClient.PacketReceived += (type, body) => BeginInvoke((Action)(() => HandleLoginPacket(type, body)));
            _worldClient.PacketReceived += (type, body) => BeginInvoke((Action)(() => HandleWorldPacket(type, body)));
            _loginClient.Log += msg => BeginInvoke((Action)(() => AppendLog("[login] " + msg)));
            _worldClient.Log += msg => BeginInvoke((Action)(() => AppendLog("[world] " + msg)));
            _loginClient.ConnectionChanged += on => BeginInvoke((Action)(() => HandleConnectionChanged(isWorld: false, on)));
            _worldClient.ConnectionChanged += on => BeginInvoke((Action)(() => HandleConnectionChanged(isWorld: true, on)));
            _loginClient.Disconnected += (reason, expected) => BeginInvoke((Action)(() => HandleSocketClosed(isWorld: false, reason, expected)));
            _worldClient.Disconnected += (reason, expected) => BeginInvoke((Action)(() => HandleSocketClosed(isWorld: true, reason, expected)));
            FormClosing += async (s, e) => await DisconnectAllAsync(DisconnectReason.FormClosing);
        }

        private async Task ConnectLoginAsync()
        {
            _state.LoginHost = _loginPanel.HostTextBox.Text.Trim();
            int.TryParse(_loginPanel.PortTextBox.Text.Trim(), out var port);
            _state.LoginPort = port <= 0 ? 26788 : port;
            await _loginClient.ConnectAsync(_state.LoginHost, _state.LoginPort);
        }

        private async Task LoginAsync()
        {
            _state.LoginId = _loginPanel.LoginIdTextBox.Text.Trim();
            _state.Password = _loginPanel.PasswordTextBox.Text;
            await SendLoginPacketAsync(ClientProtocol.LoginRequest, ClientProtocol.BuildLoginRequest(_state.LoginId, _state.Password));
        }

        private async Task SelectWorldAsync()
        {
            if (!(_characterPanel.WorldListBox.SelectedItem is WorldSummary item)) return;
            _state.SelectedWorldId = item.WorldId;
            await SendLoginPacketAsync(ClientProtocol.WorldSelectRequest, ClientProtocol.BuildWorldSelectRequest(item.WorldId, item.ServerCode));
        }

        private async Task SelectCharacterAsync()
        {
            if (!(_characterPanel.CharacterListBox.SelectedItem is CharacterSummary item)) return;
            _state.SelectedCharId = item.CharId;
            await SendLoginPacketAsync(ClientProtocol.CharacterSelectRequest, ClientProtocol.BuildCharacterSelectRequest(item.CharId));
        }

        private async Task EnterWorldAsync()
        {
            if (string.IsNullOrWhiteSpace(_state.WorldHost) || _state.WorldPort <= 0)
            {
                AppendLog("world endpoint not ready; select world and character first");
                return;
            }
            if (!_worldClient.IsConnected)
            {
                await _worldClient.ConnectAsync(_state.WorldHost, _state.WorldPort);
            }
            _state.EnterWorldRequested = true;
            _state.CurrentStage = ClientStage.EnterWorldPending;
            await SendWorldPacketAsync(ClientProtocol.EnterWorldWithToken, ClientProtocol.BuildEnterWorldRequest(_state.AccountId, _state.LoginSession, _state.WorldToken));
        }

        private async Task ReconnectWorldAsync()
        {
            if (string.IsNullOrWhiteSpace(_state.WorldHost) || _state.WorldPort <= 0 || string.IsNullOrWhiteSpace(_state.ReconnectToken))
            {
                AppendLog("reconnect token not ready; falling back to login reconnect");
                await ConnectLoginAsync();
                if (!string.IsNullOrWhiteSpace(_state.LoginId))
                {
                    await LoginAsync();
                }
                return;
            }
            if (!_worldClient.IsConnected)
            {
                await _worldClient.ConnectAsync(_state.WorldHost, _state.WorldPort);
            }
            await SendWorldPacketAsync(ClientProtocol.ReconnectWorld, ClientProtocol.BuildReconnectWorld(_state.AccountId, _state.SelectedCharId, _state.ReconnectToken));
        }

        private async Task MoveAsync()
        {
            var x = (int)_characterPanel.MoveX.Value;
            var y = (int)_characterPanel.MoveY.Value;
            _state.PosX = x;
            _state.PosY = y;
            EnsureSelfVisible();
            await SendWorldPacketAsync(ClientProtocol.Move, ClientProtocol.BuildMove(x, y));
            RefreshUi();
        }

        private async Task AttackAsync()
        {
            if (!string.IsNullOrWhiteSpace(_selectedObjectId) && _state.LiveObjects.TryGetValue(_selectedObjectId, out var selected))
            {
                if (selected is PlayerObject player && !player.IsSelf)
                {
                    await SendWorldPacketAsync(ClientProtocol.AttackPlayer, ClientProtocol.BuildAttackPlayer(player.CharId));
                    AppendLog($"attack_player requested target={player.CharId}");
                    return;
                }
            }

            if (_lastMonsterId == 0)
            {
                var monsterTemplateId = 0;
                if (!string.IsNullOrWhiteSpace(_selectedObjectId))
                {
                    WorldObject selectedObject = null;
                    if (!_state.LiveObjects.TryGetValue(_selectedObjectId, out selectedObject))
                    {
                        selectedObject = _state.StaticObjects.FirstOrDefault(o => o.Id == _selectedObjectId);
                    }

                    if (selectedObject is MonsterObject monster)
                    {
                        monsterTemplateId = Math.Max(0, monster.MonsterTemplateMin);
                    }
                }

                await SendWorldPacketAsync(ClientProtocol.SpawnMonster, ClientProtocol.BuildSpawnMonster((uint)monsterTemplateId));
                AppendLog(monsterTemplateId > 0
                    ? $"spawn_monster requested using overlay template={monsterTemplateId}"
                    : "spawn_monster requested for attack probe");
                return;
            }
            await SendWorldPacketAsync(ClientProtocol.AttackMonster, ClientProtocol.BuildAttackMonster(_lastMonsterId));
        }

        private async Task SoftLogoutAsync()
        {
            if (!_state.ConnectedToWorld)
            {
                AppendLog("soft logout skipped; world not connected");
                return;
            }
            _state.ExplicitLogoutRequested = true;
            _state.HardLogoutRequested = false;
            _state.ReconnectRequested = false;
            _state.LastReconnectTarget = ReconnectTarget.None;
            _state.ReconnectResumeStage = ClientStage.None;
            await SendWorldPacketAsync(ClientProtocol.LogoutWorld, ClientProtocol.BuildLogoutWorld(false));
            AppendLog("soft logout requested");
        }

        private async Task LogoutAsync()
        {
            if (!_state.ConnectedToWorld)
            {
                _state.ExplicitLogoutRequested = true;
                _state.HardLogoutRequested = true;
                await DisconnectAllAsync(DisconnectReason.ExplicitLogout);
                ResetWorldState(clearCredentials: false);
                AppendLog("hard logout completed from login stage");
                RefreshUi();
                return;
            }

            _state.ExplicitLogoutRequested = true;
            _state.HardLogoutRequested = true;
            _state.ReconnectRequested = false;
            _state.LastReconnectTarget = ReconnectTarget.None;
            _state.ReconnectResumeStage = ClientStage.None;
            await SendWorldPacketAsync(ClientProtocol.LogoutWorld, ClientProtocol.BuildLogoutWorld(true));
            AppendLog("hard logout requested");
        }

        private async Task ReconnectAsync()
        {
            _state.ReconnectRequested = true;
            _state.ExplicitLogoutRequested = false;
            _state.HardLogoutRequested = false;
            _state.ReconnectResumeStage = _state.LastStableStage;
            _state.LastReconnectTarget = DetermineReconnectTarget();
            var reconnectTarget = _state.LastReconnectTarget;
            AppendLog($"reconnect requested stage={_state.LastStableStage} target={reconnectTarget}");
            await DisconnectAllAsync(DisconnectReason.ManualReconnect);

            if (reconnectTarget == ReconnectTarget.World)
            {
                AppendLog(_state.WorldEnterCompleted
                    ? "reconnect target=world direct reconnect"
                    : "reconnect target=world enter retry");
                if (_state.WorldEnterCompleted)
                {
                    await ReconnectWorldAsync();
                }
                else
                {
                    await EnterWorldAsync();
                }
                return;
            }

            await ConnectLoginAsync();
            if (!string.IsNullOrWhiteSpace(_state.LoginId))
            {
                await LoginAsync();
            }
        }

        private ReconnectTarget DetermineReconnectTarget()
        {
            if (_state.CanDirectReconnectToWorld)
            {
                return ReconnectTarget.World;
            }
            return ReconnectTarget.Login;
        }

        private async Task DisconnectAllAsync(DisconnectReason reason)
        {
            _state.PendingDisconnectReason = reason;
            var expected = reason != DisconnectReason.NetworkError && reason != DisconnectReason.RemoteClosed && reason != DisconnectReason.ServerClosed;
            await _worldClient.DisconnectAsync(reason.ToString(), expected);
            await _loginClient.DisconnectAsync(reason.ToString(), expected);
        }

        private async Task SendLoginPacketAsync(ushort type, byte[] body)
        {
            if (!_loginClient.IsConnected) await ConnectLoginAsync();
            await _loginClient.SendAsync(type, body);
        }

        private async Task SendWorldPacketAsync(ushort type, byte[] body)
        {
            if (!_worldClient.IsConnected)
            {
                AppendLog("world socket not connected");
                return;
            }
            await _worldClient.SendAsync(type, body);
        }

        private void HandleConnectionChanged(bool isWorld, bool on)
        {
            if (isWorld)
            {
                _state.ConnectedToWorld = on;
                if (on)
                {
                    _state.CurrentStage = ClientStage.WorldConnected;
                }
            }
            else
            {
                _state.ConnectedToLogin = on;
                if (on && _state.CurrentStage == ClientStage.None)
                {
                    _state.CurrentStage = ClientStage.LoginConnected;
                }
            }
            RefreshUi();
        }

        private void HandleSocketClosed(bool isWorld, string reason, bool expected)
        {
            var mapped = MapDisconnectReason(reason, expected);
            _state.LastDisconnectReason = mapped;
            _state.LastDisconnectDetail = reason ?? string.Empty;
            AppendLog($"{(isWorld ? "world" : "login")} closed reason={mapped} expected={(expected ? 1 : 0)} raw={reason} reconnect_target={_state.LastReconnectTarget}");

            if (_state.PendingDisconnectReason != DisconnectReason.None)
            {
                _state.PendingDisconnectReason = DisconnectReason.None;
            }
            if (_state.ExplicitLogoutRequested)
            {
                _state.ReconnectRequested = false;
            }
            RefreshUi();
        }

        private DisconnectReason MapDisconnectReason(string reason, bool expected)
        {
            if (_state.PendingDisconnectReason != DisconnectReason.None)
            {
                return _state.PendingDisconnectReason;
            }
            if (_state.ExplicitLogoutRequested)
            {
                return DisconnectReason.ExplicitLogout;
            }
            if (string.IsNullOrWhiteSpace(reason))
            {
                return expected ? DisconnectReason.ManualReconnect : DisconnectReason.NetworkError;
            }
            if (reason.StartsWith("remote_closed", StringComparison.OrdinalIgnoreCase))
            {
                return DisconnectReason.RemoteClosed;
            }
            if (reason.StartsWith("socket_error", StringComparison.OrdinalIgnoreCase))
            {
                return DisconnectReason.NetworkError;
            }
            if (reason.StartsWith("FormClosing", StringComparison.OrdinalIgnoreCase))
            {
                return DisconnectReason.FormClosing;
            }
            return expected ? DisconnectReason.ManualReconnect : DisconnectReason.ServerClosed;
        }

        private void HandleLoginPacket(ushort type, byte[] body)
        {
            switch (type)
            {
                case ClientProtocol.LoginResult:
                    var login = ClientProtocol.ParseLoginResult(body);
                    _state.LoggedIn = login.Ok;
                    _state.AccountId = login.AccountId;
                    _state.LoginSession = login.LoginSession;
                    _state.WorldHost = string.Empty;
                    _state.WorldPort = 0;
                    _state.WorldToken = string.Empty;
                    _state.SelectedChannelId = 0;
                    _state.WorldEnterCompleted = false;
                    _state.EnterWorldRequested = false;
                    if (login.Ok)
                    {
                        _state.CurrentStage = ClientStage.LoggedIn;
                        _state.LastStableStage = ClientStage.LoggedIn;
                    }
                    _state.Worlds.Clear();
                    _state.Characters.Clear();
                    _characterPanel.WorldListBox.DataSource = null;
                    _characterPanel.CharacterListBox.DataSource = null;
                    AppendLog($"login_result ok={login.Ok} account_id={login.AccountId} world_entries={login.WorldEntries} fail_reason={login.FailReason} waiting_world_select=1");
                    if (login.Ok)
                    {
                        _ = SendLoginPacketAsync(ClientProtocol.WorldListRequest, ClientProtocol.BuildWorldListRequest());
                    }
                    break;
                case ClientProtocol.WorldListResponse:
                    bool worldsOk;
                    string worldsReason;
                    var worlds = ClientProtocol.ParseWorldList(body, out worldsOk, out worldsReason);
                    _state.Worlds.Clear();
                    _state.Worlds.AddRange(worlds);
                    _characterPanel.WorldListBox.DataSource = null;
                    _characterPanel.WorldListBox.DataSource = _state.Worlds.ToList();
                    AppendLog($"world_list ok={worldsOk} count={worlds.Count} reason={worldsReason}");
                    if (_state.ReconnectRequested && _state.ReconnectResumeStage >= ClientStage.WorldSelected && _state.SelectedWorldId != 0)
                    {
                        var selected = _state.Worlds.FirstOrDefault(x => x.WorldId == _state.SelectedWorldId);
                        if (selected != null)
                        {
                            _characterPanel.WorldListBox.SelectedItem = selected;
                            _ = SelectWorldAsync();
                        }
                    }
                    break;
                case ClientProtocol.WorldSelectResponse:
                    var worldSelect = ClientProtocol.ParseWorldSelect(body);
                    if (worldSelect.Ok)
                    {
                        _state.SelectedWorldId = worldSelect.WorldId;
                        _state.SelectedChannelId = 0;
                        _state.WorldHost = worldSelect.WorldHost;
                        _state.WorldPort = worldSelect.WorldPort;
                        _state.WorldToken = string.Empty;
                        _state.Characters.Clear();
                        _characterPanel.CharacterListBox.DataSource = null;
                        _state.CurrentStage = ClientStage.WorldSelected;
                        _state.LastStableStage = ClientStage.WorldSelected;
                    }
                    AppendLog($"world_select ok={worldSelect.Ok} world={worldSelect.WorldId} server_code={worldSelect.ServerCode} fail_reason={worldSelect.FailReason}");
                    if (worldSelect.Ok && _state.ReconnectRequested && _state.ReconnectResumeStage >= ClientStage.CharacterSelected)
                    {
                        _ = SendLoginPacketAsync(ClientProtocol.CharacterListRequest, ClientProtocol.BuildCharacterListRequest());
                    }
                    break;
                case ClientProtocol.CharacterListResponse:
                    bool ok;
                    string reason;
                    var list = ClientProtocol.ParseCharacterList(body, out ok, out reason);
                    _state.Characters.Clear();
                    _state.Characters.AddRange(list);
                    _characterPanel.CharacterListBox.DataSource = null;
                    _characterPanel.CharacterListBox.DataSource = _state.Characters.ToList();
                    AppendLog($"character_list ok={ok} count={list.Count} reason={reason}");
                    if (_state.ReconnectRequested && _state.ReconnectResumeStage >= ClientStage.CharacterSelected && _state.SelectedCharId != 0)
                    {
                        var selected = _state.Characters.FirstOrDefault(x => x.CharId == _state.SelectedCharId);
                        if (selected != null)
                        {
                            _characterPanel.CharacterListBox.SelectedItem = selected;
                            _ = SelectCharacterAsync();
                        }
                    }
                    break;
                case ClientProtocol.CharacterSelectResponse:
                    var select = ClientProtocol.ParseCharacterSelect(body);
                    if (select.Ok)
                    {
                        _state.SelectedCharId = select.CharId;
                        _state.WorldHost = select.WorldHost;
                        _state.WorldPort = select.WorldPort;
                        _state.WorldToken = select.WorldToken;
                        _state.WorldEnterCompleted = false;
                        _state.CurrentStage = ClientStage.CharacterSelected;
                        _state.LastStableStage = ClientStage.CharacterSelected;
                    }
                    AppendLog($"character_select ok={select.Ok} char_id={select.CharId} fail_reason={select.FailReason}");
                    if (select.Ok && _state.ReconnectRequested && _state.ReconnectResumeStage >= ClientStage.InWorld)
                    {
                        _ = EnterWorldAsync();
                    }
                    break;
                default:
                    AppendLog($"unknown login packet type={type} size={body.Length}");
                    break;
            }
            RefreshUi();
        }

        private void HandleWorldPacket(ushort type, byte[] body)
        {
            switch (type)
            {
                case ClientProtocol.EnterWorldResult:
                    var enter = ClientProtocol.ParseEnterWorldResult(body);
                    _state.InWorld = enter.Ok;
                    _state.EnterWorldRequested = false;
                    _state.WorldEnterCompleted = enter.Ok;
                    if (enter.Ok)
                    {
                        _state.CurrentStage = ClientStage.InWorld;
                        _state.LastStableStage = ClientStage.InWorld;
                        _state.ReconnectToken = enter.ReconnectToken ?? string.Empty;
                    }
                    else
                    {
                        _state.ReconnectToken = string.Empty;
                    }
                    if (enter.CharId != 0) _state.SelectedCharId = enter.CharId;
                    EnsureSelfVisible();
                    AppendLog($"enter_world ok={enter.Ok} reason={enter.Reason} char_id={enter.CharId} reconnect_token_len={(_state.ReconnectToken?.Length ?? 0)}");
                    if (_state.ReconnectRequested && enter.Ok)
                    {
                        AppendLog("reconnect resume completed");
                        _state.ReconnectRequested = false;
                        _state.ReconnectResumeStage = ClientStage.None;
                    }
                    break;
                case ClientProtocol.LogoutWorldResult:
                    var logout = ClientProtocol.ParseLogoutWorldResult(body);
                    AppendLog($"logout_world_result ok={logout.Ok} type={logout.Type} reason={logout.Reason} char_id={logout.CharId}");
                    if (logout.Ok)
                    {
                        _state.ReconnectRequested = false;
                        _state.ReconnectResumeStage = ClientStage.None;
                        _state.LastReconnectTarget = ReconnectTarget.None;
                        _ = _worldClient.DisconnectAsync("explicit_logout_world", true);
                        ResetRuntimeStateAfterWorldLogout(logout.Type == 2);
                        if (logout.Type == 2)
                        {
                            _ = _loginClient.DisconnectAsync("explicit_logout_login", true);
                            ResetWorldState(clearCredentials: false);
                            _state.CurrentStage = ClientStage.None;
                        }
                    }
                    break;
                case ClientProtocol.ReconnectWorldResult:
                    var reconnect = ClientProtocol.ParseReconnectWorldResult(body);
                    AppendLog($"reconnect_world_result ok={reconnect.Ok} reason={reconnect.Reason} char_id={reconnect.CharId} zone={reconnect.ZoneId} map={reconnect.MapId} pos=({reconnect.X},{reconnect.Y})");
                    if (reconnect.Ok)
                    {
                        _state.InWorld = true;
                        _state.WorldEnterCompleted = true;
                        _state.CurrentStage = ClientStage.InWorld;
                        _state.LastStableStage = ClientStage.InWorld;
                        _state.ReconnectRequested = false;
                        _state.ReconnectResumeStage = ClientStage.None;
                        _state.LastReconnectTarget = ReconnectTarget.World;
                        _state.ReconnectToken = reconnect.ReconnectToken ?? string.Empty;
                        if (reconnect.CharId != 0) _state.SelectedCharId = reconnect.CharId;
                        ApplyZoneMapState(new ZoneMapStateModel { CharId = reconnect.CharId, ZoneId = reconnect.ZoneId, MapId = reconnect.MapId, X = reconnect.X, Y = reconnect.Y, Reason = 0 });
                        EnsureSelfVisible();
                    }
                    break;
                case ClientProtocol.ZoneMapState:
                    var zoneState = ClientProtocol.ParseZoneMapState(body);
                    ApplyZoneMapState(zoneState);
                    AppendLog($"zone_map_state char_id={zoneState.CharId} zone={zoneState.ZoneId} map={zoneState.MapId} pos=({zoneState.X},{zoneState.Y}) reason={zoneState.Reason}");
                    break;
                case ClientProtocol.Stats:
                    var stats = ClientProtocol.ParseStats(body);
                    _state.SelectedCharId = stats.CharId;
                    _state.Hp = stats.Hp;
                    _state.MaxHp = stats.MaxHp;
                    _state.Atk = stats.Atk;
                    _state.Def = stats.Def;
                    _state.Gold = stats.Gold;
                    EnsureSelfVisible();
                    AppendLog($"stats hp={stats.Hp}/{stats.MaxHp} atk={stats.Atk} def={stats.Def} gold={stats.Gold}");
                    break;
                case ClientProtocol.AddGoldOk:
                    var gold = ClientProtocol.ParseAddGoldResult(body);
                    _state.Gold = gold.Gold;
                    AppendLog($"add_gold ok={gold.Ok} gold={gold.Gold}");
                    break;
                case ClientProtocol.SpawnMonsterOk:
                    var sm = ClientProtocol.ParseSpawnMonsterOk(body);
                    _lastMonsterId = sm.MonsterId;
                    AppendLog($"spawn_monster_ok monster_id={sm.MonsterId} hp={sm.Hp} atk={sm.Atk} def={sm.Def}");
                    break;
                case ClientProtocol.AttackResult:
                    var ar = ClientProtocol.ParseAttackResult(body);
                    _state.Gold = ar.AttackerGold;
                    AppendLog($"attack_result target={ar.TargetId} dmg={ar.Damage} target_hp={ar.TargetHp} killed={ar.Killed} drop={ar.DropItemId}x{ar.DropCount}");
                    break;
                case ClientProtocol.PlayerSpawn:
                    UpsertPlayer(ClientProtocol.ParsePlayerSpawn(body, _state.SelectedCharId));
                    AppendLog($"player_spawn live_objects={_state.LiveObjects.Count}");
                    break;
                case ClientProtocol.PlayerMove:
                    UpsertPlayer(ClientProtocol.ParsePlayerMove(body, _state.SelectedCharId));
                    AppendLog($"player_move live_objects={_state.LiveObjects.Count}");
                    break;
                case ClientProtocol.PlayerSpawnBatch:
                    var spawnBatch = ClientProtocol.ParsePlayerSpawnBatch(body, _state.SelectedCharId);
                    foreach (var p in spawnBatch) UpsertPlayer(p);
                    AppendLog($"player_spawn_batch count={spawnBatch.Count} live_objects={_state.LiveObjects.Count}");
                    break;
                case ClientProtocol.PlayerMoveBatch:
                    var moveBatch = ClientProtocol.ParsePlayerMoveBatch(body, _state.SelectedCharId);
                    foreach (var p in moveBatch) UpsertPlayer(p);
                    AppendLog($"player_move_batch count={moveBatch.Count} live_objects={_state.LiveObjects.Count}");
                    break;
                case ClientProtocol.PlayerDespawn:
                    RemovePlayer(ClientProtocol.ParsePlayerDespawn(body));
                    AppendLog($"player_despawn live_objects={_state.LiveObjects.Count}");
                    break;
                case ClientProtocol.PlayerDespawnBatch:
                    var despawnBatch = ClientProtocol.ParsePlayerDespawnBatch(body);
                    foreach (var id in despawnBatch) RemovePlayer(id);
                    AppendLog($"player_despawn_batch count={despawnBatch.Count} live_objects={_state.LiveObjects.Count}");
                    break;
                default:
                    AppendLog($"unknown world packet type={type} size={body.Length}");
                    break;
            }
            RefreshUi();
        }

        private void UpsertPlayer(PlayerObject obj)
        {
            if (obj.CharId == _state.SelectedCharId)
            {
                _state.PosX = obj.X;
                _state.PosY = obj.Y;
                obj.IsSelf = true;
                obj.Label = "Self";
                obj.ZoneId = _state.ZoneId;
                obj.MapId = _state.MapId;
            }
            _state.LiveObjects[obj.Id] = obj;
        }

        private void EnsureSelfVisible()
        {
            if (_state.SelectedCharId == 0) return;
            _state.LiveObjects["player:" + _state.SelectedCharId] = new PlayerObject
            {
                Kind = WorldObjectKind.Player,
                CharId = _state.SelectedCharId,
                Id = "player:" + _state.SelectedCharId,
                X = _state.PosX,
                Y = _state.PosY,
                ZoneId = _state.ZoneId,
                MapId = _state.MapId,
                Label = "Self",
                IsSelf = true,
            };
        }

        private void RemovePlayer(ulong charId)
        {
            if (charId == _state.SelectedCharId)
            {
                EnsureSelfVisible();
                return;
            }
            _state.LiveObjects.Remove("player:" + charId);
        }

        private void ApplyZoneMapState(ZoneMapStateModel zoneState)
        {
            if (zoneState.CharId != 0) _state.SelectedCharId = zoneState.CharId;
            _state.ZoneId = (int)zoneState.ZoneId;
            _state.MapId = (int)zoneState.MapId;
            _state.PosX = zoneState.X;
            _state.PosY = zoneState.Y;
            _state.ZoneMapStateReason = zoneState.Reason;
            EnsureSelfVisible();
            ApplyOverlaySelectionFromState();
        }

        private void ResetRuntimeStateAfterWorldLogout(bool hardLogout)
        {
            _state.ConnectedToWorld = false;
            _state.InWorld = false;
            _state.WorldEnterCompleted = false;
            _state.EnterWorldRequested = false;
            _state.ReconnectToken = string.Empty;
            _state.ZoneMapStateReason = 0;
            _state.PosX = 0;
            _state.PosY = 0;
            _state.Hp = 0;
            _state.MaxHp = 0;
            _state.Atk = 0;
            _state.Def = 0;
            _state.Gold = 0;
            _state.LiveObjects.Clear();
            _selectedObjectId = string.Empty;
            _lastMonsterId = 0;

            if (hardLogout)
            {
                _state.CurrentStage = ClientStage.None;
                _state.LastStableStage = ClientStage.None;
                _state.SelectedCharId = 0;
                _state.SelectedWorldId = 0;
                _state.WorldToken = string.Empty;
                _state.Worlds.Clear();
                _state.Characters.Clear();
                _characterPanel.WorldListBox.DataSource = null;
                _characterPanel.CharacterListBox.DataSource = null;
            }
            else
            {
                _state.CurrentStage = _state.SelectedCharId != 0 ? ClientStage.CharacterSelected : ClientStage.WorldSelected;
                _state.LastStableStage = _state.CurrentStage;
            }
        }

        private void ApplyOverlaySelectionFromState()
        {
            if (!_state.AutoOverlayZone) return;
            var overlayZone = _state.ZoneId > 0 ? _state.ZoneId : _state.MapId;
            if (overlayZone <= 0) return;
            var clamped = Math.Max((int)_worldPanel.ZoneSelector.Minimum, Math.Min((int)_worldPanel.ZoneSelector.Maximum, overlayZone));
            if ((int)_worldPanel.ZoneSelector.Value != clamped)
            {
                _worldPanel.ZoneSelector.Value = clamped;
            }
            LoadZoneOverlay(clamped, false);
        }

        private void LoadZoneOverlay(int zoneId)
        {
            LoadZoneOverlay(zoneId, true);
        }

        private void LoadZoneOverlay(int zoneId, bool manualSelection)
        {
            if (manualSelection)
            {
                _state.ZoneId = zoneId;
                _state.MapId = zoneId;
            }
            _state.StaticObjects.Clear();
            _state.StaticObjects.AddRange(_gameDataService.LoadZoneOverlay(zoneId));
            AppendLog($"zone overlay loaded zone={zoneId} static_objects={_state.StaticObjects.Count} mode={(_state.AutoOverlayZone ? "auto" : "manual")} data_root={_gameDataService.DataRoot}");
            RefreshUi();
        }

        private void SelectNearestObject(Point click)
        {
            var all = _state.StaticObjects.Concat(_state.LiveObjects.Values).ToList();
            if (all.Count == 0) return;
            var center = new Point(_state.PosX, _state.PosY);
            var nearest = all.OrderBy(o => DistanceSquared(_renderService.WorldToScreen(center, _worldPanel.Canvas.ClientRectangle, o.ToPoint()), click)).First();
            _selectedObjectId = nearest.Id;
            _worldPanel.SelectionLabel.Text = $"Selected: {nearest.Kind} {nearest.Label} zone={nearest.ZoneId} map={nearest.MapId} pos=({nearest.X},{nearest.Y}) r={nearest.Radius}";
            RefreshUi();
        }

        private static double DistanceSquared(PointF a, Point b)
        {
            var dx = a.X - b.X;
            var dy = a.Y - b.Y;
            return dx * dx + dy * dy;
        }

        private void ResetWorldState(bool clearCredentials)
        {
            _state.ConnectedToLogin = false;
            _state.ConnectedToWorld = false;
            _state.LoggedIn = false;
            _state.InWorld = false;
            _state.AccountId = 0;
            _state.SelectedCharId = 0;
            _state.SelectedWorldId = 0;
            _state.SelectedChannelId = 0;
            _state.LoginSession = string.Empty;
            _state.WorldToken = string.Empty;
            _state.ReconnectToken = string.Empty;
            _state.WorldHost = string.Empty;
            _state.WorldPort = 0;
            _state.WorldEnterCompleted = false;
            _state.EnterWorldRequested = false;
            _state.CurrentStage = ClientStage.None;
            _state.LastStableStage = ClientStage.None;
            _state.ReconnectResumeStage = ClientStage.None;
            _state.Worlds.Clear();
            _state.Characters.Clear();
            _state.LiveObjects.Clear();
            _characterPanel.WorldListBox.DataSource = null;
            _characterPanel.CharacterListBox.DataSource = null;
            _lastMonsterId = 0;
            if (clearCredentials)
            {
                _state.LoginId = string.Empty;
                _state.Password = string.Empty;
            }
        }

        private void AppendLog(string message)
        {
            _logTextBox.AppendText($"[{DateTime.Now:HH:mm:ss}] {message}{Environment.NewLine}");
            _topStatusLabel.Text = message;
        }

        private void RefreshUi()
        {
            _loginPanel.StatusLabel.Text = $"login={(_state.ConnectedToLogin ? "connected" : "down")}, world={(_state.ConnectedToWorld ? "connected" : "down")}";
            _stateTextBox.Text =
                $"Account: {_state.AccountId}{Environment.NewLine}" +
                $"SelectedWorld: {_state.SelectedWorldId}/{_state.SelectedChannelId}{Environment.NewLine}" +
                $"SelectedChar: {_state.SelectedCharId}{Environment.NewLine}" +
                $"Stage: {_state.CurrentStage} stable={_state.LastStableStage}{Environment.NewLine}" +
                $"LoggedIn: {_state.LoggedIn}{Environment.NewLine}" +
                $"InWorld: {_state.InWorld}{Environment.NewLine}" +
                $"World: {_state.WorldHost}:{_state.WorldPort}{Environment.NewLine}" +
                $"ReconnectTarget: {_state.LastReconnectTarget}{Environment.NewLine}" +
                $"ReconnectResumeStage: {_state.ReconnectResumeStage}{Environment.NewLine}" +
                $"DisconnectReason: {_state.LastDisconnectReason}{Environment.NewLine}" +
                $"DisconnectDetail: {_state.LastDisconnectDetail}{Environment.NewLine}" +
                $"CoordScale: 1u={_state.CoordinateUnitMeters}m{Environment.NewLine}" +
                $"AOIRadius: {_state.AoiRadiusMeters}m{Environment.NewLine}" +
                $"Zone/Map: {_state.ZoneId}/{_state.MapId}{Environment.NewLine}" +
                $"OverlayMode: {(_state.AutoOverlayZone ? "auto" : "manual")}{Environment.NewLine}" +
                $"ZoneMapReason: {_state.ZoneMapStateReason}{Environment.NewLine}" +
                $"Pos: {_state.PosX}, {_state.PosY}{Environment.NewLine}" +
                $"HP: {_state.Hp}/{_state.MaxHp}{Environment.NewLine}" +
                $"ATK/DEF: {_state.Atk}/{_state.Def}{Environment.NewLine}" +
                $"Gold: {_state.Gold}{Environment.NewLine}" +
                $"WorldCount: {_state.Worlds.Count}{Environment.NewLine}" +
                $"LiveObjects: {_state.LiveObjects.Count}{Environment.NewLine}" +
                $"StaticObjects: {_state.StaticObjects.Count}{Environment.NewLine}" +
                $"DataRoot: {_gameDataService.DataRoot}";
            _worldPanel.Canvas.Invalidate();
        }
    }
}





