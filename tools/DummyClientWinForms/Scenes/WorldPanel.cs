using System;
using System.Drawing;
using System.Windows.Forms;

namespace DummyClientWinForms.Scenes
{
    public sealed class WorldPanel : UserControl
    {
        public event Action<int> ZoneOverlayChanged;
        public event Action<bool> AutoOverlayChanged;

        public PictureBox Canvas { get; } = new PictureBox { Dock = DockStyle.Fill, BackColor = Color.FromArgb(24, 26, 30) };
        public NumericUpDown ZoneSelector { get; } = new NumericUpDown { Dock = DockStyle.Top, Minimum = 1, Maximum = 300, Value = 1 };
        public CheckBox AutoOverlayCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "Auto overlay zone" };
        public Label SelectionLabel { get; } = new Label { Dock = DockStyle.Bottom, Height = 48, Text = "Selected: none" };

        public WorldPanel()
        {
            Dock = DockStyle.Fill;
            Canvas.SizeMode = PictureBoxSizeMode.Normal;
            ZoneSelector.ValueChanged += (s, e) => ZoneOverlayChanged?.Invoke((int)ZoneSelector.Value);
            AutoOverlayCheckBox.CheckedChanged += (s, e) => AutoOverlayChanged?.Invoke(AutoOverlayCheckBox.Checked);
            Controls.Add(Canvas);
            Controls.Add(AutoOverlayCheckBox);
            Controls.Add(new Label { Dock = DockStyle.Top, Height = 18, Text = "Overlay Zone" });
            Controls.Add(ZoneSelector);
            Controls.Add(SelectionLabel);
            Padding = new Padding(8);
        }
    }
}
