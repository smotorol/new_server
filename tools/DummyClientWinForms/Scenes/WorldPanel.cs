using System;
using System.Drawing;
using System.Windows.Forms;

namespace DummyClientWinForms.Scenes
{
    public sealed class WorldPanel : UserControl
    {
        public event Action<int> ZoneOverlayChanged;
        public event Action<bool> AutoOverlayChanged;
        public event Action<bool> GeometryOverlayChanged;
        public event Action MarkerOverlayChanged;
        public event Action<bool> MiniMapChanged;
        public event Action<bool> MiniMapLegendChanged;

        public PictureBox Canvas { get; } = new PictureBox { Dock = DockStyle.Fill, BackColor = Color.FromArgb(24, 26, 30), TabStop = true };
        public PictureBox MiniMapCanvas { get; } = new PictureBox { Dock = DockStyle.Fill, BackColor = Color.FromArgb(18, 20, 24) };
        public NumericUpDown ZoneSelector { get; } = new NumericUpDown { Dock = DockStyle.Top, Minimum = 1, Maximum = 300, Value = 1 };
        public CheckBox AutoOverlayCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "Auto overlay zone" };
        public CheckBox GeometryOverlayCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "Geometry overlay" };
        public CheckBox PortalMarkersCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "Portal markers" };
        public CheckBox SafeZoneMarkersCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "Safe zone markers" };
        public CheckBox SpawnMarkersCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "Spawn markers" };
        public CheckBox MiniMapCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "MiniMap" };
        public CheckBox MiniMapLegendCheckBox { get; } = new CheckBox { Dock = DockStyle.Top, Checked = true, Text = "MiniMap legend" };
        public Label SelectionLabel { get; } = new Label { Dock = DockStyle.Bottom, Height = 48, Text = "Selected: none" };

        public WorldPanel()
        {
            Dock = DockStyle.Fill;
            Canvas.SizeMode = PictureBoxSizeMode.Normal;
            MiniMapCanvas.SizeMode = PictureBoxSizeMode.Normal;

            ZoneSelector.ValueChanged += (s, e) => ZoneOverlayChanged?.Invoke((int)ZoneSelector.Value);
            AutoOverlayCheckBox.CheckedChanged += (s, e) => AutoOverlayChanged?.Invoke(AutoOverlayCheckBox.Checked);
            GeometryOverlayCheckBox.CheckedChanged += (s, e) => GeometryOverlayChanged?.Invoke(GeometryOverlayCheckBox.Checked);
            PortalMarkersCheckBox.CheckedChanged += (s, e) => MarkerOverlayChanged?.Invoke();
            SafeZoneMarkersCheckBox.CheckedChanged += (s, e) => MarkerOverlayChanged?.Invoke();
            SpawnMarkersCheckBox.CheckedChanged += (s, e) => MarkerOverlayChanged?.Invoke();
            MiniMapCheckBox.CheckedChanged += (s, e) => MiniMapChanged?.Invoke(MiniMapCheckBox.Checked);
            MiniMapLegendCheckBox.CheckedChanged += (s, e) => MiniMapLegendChanged?.Invoke(MiniMapLegendCheckBox.Checked);

            var sidePanel = new Panel { Dock = DockStyle.Right, Width = 280, Padding = new Padding(6) };
            sidePanel.Controls.Add(MiniMapCanvas);
            sidePanel.Controls.Add(MiniMapLegendCheckBox);
            sidePanel.Controls.Add(MiniMapCheckBox);
            sidePanel.Controls.Add(SpawnMarkersCheckBox);
            sidePanel.Controls.Add(SafeZoneMarkersCheckBox);
            sidePanel.Controls.Add(PortalMarkersCheckBox);
            sidePanel.Controls.Add(GeometryOverlayCheckBox);
            sidePanel.Controls.Add(AutoOverlayCheckBox);
            sidePanel.Controls.Add(new Label { Dock = DockStyle.Top, Height = 18, Text = "Overlay Zone" });
            sidePanel.Controls.Add(ZoneSelector);

            Controls.Add(Canvas);
            Controls.Add(sidePanel);
            Controls.Add(SelectionLabel);
            Padding = new Padding(8);
        }
    }
}
