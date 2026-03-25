using System;
using System.Windows.Forms;

namespace DummyClientWinForms.Scenes
{
    public sealed class CharacterPanel : UserControl
    {
        public event Action LoadWorldsRequested;
        public event Action SelectWorldRequested;
        public event Action LoadCharactersRequested;
        public event Action SelectCharacterRequested;
        public event Action EnterWorldRequested;
        public event Action StatsRequested;
        public event Action HealRequested;
        public event Action GoldRequested;
        public event Action ReconnectRequested;
        public event Action AttackRequested;
        public event Action MoveRequested;

        public ListBox WorldListBox { get; } = new ListBox { Dock = DockStyle.Top, Height = 120 };
        public ListBox CharacterListBox { get; } = new ListBox { Dock = DockStyle.Fill };
        public NumericUpDown MoveX { get; } = new NumericUpDown { Dock = DockStyle.Top, Minimum = -200000, Maximum = 200000, Increment = 50 };
        public NumericUpDown MoveY { get; } = new NumericUpDown { Dock = DockStyle.Top, Minimum = -200000, Maximum = 200000, Increment = 50 };

        public CharacterPanel()
        {
            Dock = DockStyle.Fill;
            var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 170, AutoScroll = true };
            buttons.Controls.Add(MakeButton("Worlds", () => LoadWorldsRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("PickWorld", () => SelectWorldRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Chars", () => LoadCharactersRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Select", () => SelectCharacterRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Enter", () => EnterWorldRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Stats", () => StatsRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Move", () => MoveRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Attack", () => AttackRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Heal", () => HealRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Gold+", () => GoldRequested?.Invoke()));
            buttons.Controls.Add(MakeButton("Reconnect", () => ReconnectRequested?.Invoke()));
            Controls.Add(CharacterListBox);
            Controls.Add(new Label { Dock = DockStyle.Top, Height = 18, Text = "Worlds" });
            Controls.Add(WorldListBox);
            Controls.Add(buttons);
            Controls.Add(new Label { Dock = DockStyle.Bottom, Height = 18, Text = "Move Y" }); Controls.Add(MoveY);
            Controls.Add(new Label { Dock = DockStyle.Bottom, Height = 18, Text = "Move X" }); Controls.Add(MoveX);
            Padding = new Padding(8);
        }

        private static Button MakeButton(string text, Action onClick)
        {
            var btn = new Button { Width = 76, Height = 28, Text = text };
            btn.Click += (s, e) => onClick();
            return btn;
        }
    }
}
