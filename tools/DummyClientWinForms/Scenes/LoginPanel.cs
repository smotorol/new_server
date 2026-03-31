using System;
using System.Drawing;
using System.Windows.Forms;

namespace DummyClientWinForms.Scenes
{
    public sealed class LoginPanel : UserControl
    {
        public event Action ConnectRequested;
        public event Action LoginRequested;

        public TextBox HostTextBox { get; } = new TextBox { Dock = DockStyle.Top, Text = "127.0.0.1" };
        public TextBox PortTextBox { get; } = new TextBox { Dock = DockStyle.Top, Text = "26788" };
        public TextBox LoginIdTextBox { get; } = new TextBox { Dock = DockStyle.Top, Text = "test1" };
        public TextBox PasswordTextBox { get; } = new TextBox { Dock = DockStyle.Top, Text = "pw1", UseSystemPasswordChar = true };
        public Label StatusLabel { get; } = new Label { Dock = DockStyle.Top, Height = 24, Text = "disconnected" };

        public LoginPanel()
        {
            Dock = DockStyle.Fill;
            var connect = new Button { Dock = DockStyle.Top, Height = 28, Text = "Connect" };
            var login = new Button { Dock = DockStyle.Top, Height = 28, Text = "Login" };
            connect.Click += (s, e) => ConnectRequested?.Invoke();
            login.Click += (s, e) => LoginRequested?.Invoke();
            Controls.Add(login);
            Controls.Add(connect);
            Controls.Add(new Label { Dock = DockStyle.Top, Height = 18, Text = "Password" }); Controls.Add(PasswordTextBox);
            Controls.Add(new Label { Dock = DockStyle.Top, Height = 18, Text = "Login Id" }); Controls.Add(LoginIdTextBox);
            Controls.Add(new Label { Dock = DockStyle.Top, Height = 18, Text = "Port" }); Controls.Add(PortTextBox);
            Controls.Add(new Label { Dock = DockStyle.Top, Height = 18, Text = "Host" }); Controls.Add(HostTextBox);
            Controls.Add(StatusLabel);
            Padding = new Padding(8);
        }
    }
}
