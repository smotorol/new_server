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
            _loginClient.ConnectionChanged += on => BeginInvoke((Action)(() => { _state.ConnectedToLogin = on; RefreshUi(); }));
            _worldClient.ConnectionChanged += on => BeginInvoke((Action)(() => { _state.ConnectedToWorld = on; RefreshUi(); }));
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
            await SendWorldPacketAsync(ClientProtocol.EnterWorldWithToken, ClientProtocol.BuildEnterWorldRequest(_state.AccountId, _state.LoginSession, _state.WorldToken));
        }

        private async Task MoveAsync()
        {
            var x = (int)_characterPanel.MoveX.Value;
            var y = (int)_characterPanel.MoveY.Value;
            _state.PosX = x;
            _state.PosY = y;
            await SendWorldPacketAsync(ClientProtocol.Move, ClientProtocol.BuildMove(x, y));
            RefreshUi();
        }

        private async Task AttackAsync()
        {
            if (_lastMonsterId == 0)
            {
                await SendWorldPacketAsync(ClientProtocol.SpawnMonster, ClientProtocol.BuildSpawnMonster(0));
                AppendLog("spawn_monster requested for attack probe");
                return;
            }
            await SendWorldPacketAsync(ClientProtocol.AttackMonster, ClientProtocol.BuildAttackMonster(_lastMonsterId));
        }

        private async Task ReconnectAsync()
        {
            await _worldClient.DisconnectAsync();
            await _loginClient.DisconnectAsync();
            _state.InWorld = false;
            _state.LoggedIn = false;
            _state.Worlds.Clear();
            _state.Characters.Clear();
            _characterPanel.WorldListBox.DataSource = null;
            _characterPanel.CharacterListBox.DataSource = null;
            _state.LiveObjects.Clear();
            _lastMonsterId = 0;
            await ConnectLoginAsync();
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
                    _state.SelectedWorldId = 0;
                    _state.SelectedChannelId = 0;
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
                    }
                    AppendLog($"world_select ok={worldSelect.Ok} world={worldSelect.WorldId} server_code={worldSelect.ServerCode} fail_reason={worldSelect.FailReason}");
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
                    break;
                case ClientProtocol.CharacterSelectResponse:
                    var select = ClientProtocol.ParseCharacterSelect(body);
                    if (select.Ok)
                    {
                        _state.SelectedCharId = select.CharId;
                        _state.WorldHost = select.WorldHost;
                        _state.WorldPort = select.WorldPort;
                        _state.WorldToken = select.WorldToken;
                    }
                    AppendLog($"character_select ok={select.Ok} char_id={select.CharId} fail_reason={select.FailReason}");
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
                    if (enter.CharId != 0) _state.SelectedCharId = enter.CharId;
                    AppendLog($"enter_world ok={enter.Ok} reason={enter.Reason} char_id={enter.CharId}");
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
                    AppendLog($"stats hp={stats.Hp}/{stats.MaxHp} atk={stats.Atk} def={stats.Def} gold={stats.Gold}");
                    break;
                case ClientProtocol.SpawnMonsterOk:
                    var sm = ClientProtocol.ParseSpawnMonsterOk(body);
                    _lastMonsterId = sm.MonsterId;
                    AppendLog($"spawn_monster_ok monster_id={sm.MonsterId} hp={sm.Hp} atk={sm.Atk} def={sm.Def}");
                    break;
                case ClientProtocol.AttackResult:
                    var ar = ClientProtocol.ParseAttackResult(body);
                    _state.Gold = ar.AttackerGold;
                    AppendLog($"attack_result target={ar.TargetId} dmg={ar.Damage} target_hp={ar.TargetHp} killed={ar.Killed}");
                    break;
                case ClientProtocol.PlayerSpawn:
                    UpsertPlayer(ClientProtocol.ParsePlayerSpawn(body, PacketReader.ReadUInt64(body, 0) == _state.SelectedCharId));
                    break;
                case ClientProtocol.PlayerMove:
                    UpsertPlayer(ClientProtocol.ParsePlayerSpawn(body, PacketReader.ReadUInt64(body, 0) == _state.SelectedCharId));
                    break;
                case ClientProtocol.PlayerSpawnBatch:
                    foreach (var p in ClientProtocol.ParsePlayerSpawnBatch(body, _state.SelectedCharId)) UpsertPlayer(p);
                    break;
                case ClientProtocol.PlayerMoveBatch:
                    foreach (var p in ClientProtocol.ParsePlayerSpawnBatch(body, _state.SelectedCharId)) UpsertPlayer(p);
                    break;
                case ClientProtocol.PlayerDespawn:
                    RemovePlayer(PacketReader.ReadUInt64(body, 0));
                    break;
                case ClientProtocol.PlayerDespawnBatch:
                    foreach (var id in ClientProtocol.ParsePlayerDespawnBatch(body)) RemovePlayer(id);
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
            }
            _state.LiveObjects[obj.Id] = obj;
        }

        private void RemovePlayer(ulong charId)
        {
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
            ApplyOverlaySelectionFromState();
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
                $"LoggedIn: {_state.LoggedIn}{Environment.NewLine}" +
                $"InWorld: {_state.InWorld}{Environment.NewLine}" +
                $"World: {_state.WorldHost}:{_state.WorldPort}{Environment.NewLine}" +
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

