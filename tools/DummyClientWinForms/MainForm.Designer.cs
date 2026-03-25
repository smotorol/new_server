using System.Windows.Forms;
using DummyClientWinForms.Scenes;

namespace DummyClientWinForms
{
    partial class MainForm
    {
        private SplitContainer _rootSplit;
        private SplitContainer _leftSplit;
        private SplitContainer _rightSplit;
        private LoginPanel _loginPanel;
        private CharacterPanel _characterPanel;
        private WorldPanel _worldPanel;
        private TextBox _logTextBox;
        private TextBox _stateTextBox;
        private Label _topStatusLabel;

        private void InitializeComponent()
        {
            _rootSplit = new SplitContainer();
            _leftSplit = new SplitContainer();
            _rightSplit = new SplitContainer();
            _loginPanel = new LoginPanel();
            _characterPanel = new CharacterPanel();
            _worldPanel = new WorldPanel();
            _logTextBox = new TextBox();
            _stateTextBox = new TextBox();
            _topStatusLabel = new Label();
            ((System.ComponentModel.ISupportInitialize)(_rootSplit)).BeginInit();
            _rootSplit.Panel1.SuspendLayout();
            _rootSplit.Panel2.SuspendLayout();
            _rootSplit.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(_leftSplit)).BeginInit();
            _leftSplit.Panel1.SuspendLayout();
            _leftSplit.Panel2.SuspendLayout();
            _leftSplit.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(_rightSplit)).BeginInit();
            _rightSplit.Panel1.SuspendLayout();
            _rightSplit.Panel2.SuspendLayout();
            _rightSplit.SuspendLayout();
            SuspendLayout();
            _topStatusLabel.Dock = DockStyle.Top;
            _topStatusLabel.Height = 24;
            _topStatusLabel.Text = "Dummy client ready";
            _rootSplit.Dock = DockStyle.Fill;
            _rootSplit.SplitterDistance = 320;
            _rootSplit.Panel1.Controls.Add(_leftSplit);
            _rootSplit.Panel2.Controls.Add(_rightSplit);
            _leftSplit.Dock = DockStyle.Fill;
            _leftSplit.Orientation = Orientation.Horizontal;
            _leftSplit.SplitterDistance = 210;
            _leftSplit.Panel1.Controls.Add(_loginPanel);
            _leftSplit.Panel2.Controls.Add(_characterPanel);
            _rightSplit.Dock = DockStyle.Fill;
            _rightSplit.SplitterDistance = 760;
            _rightSplit.Panel1.Controls.Add(_worldPanel);
            _rightSplit.Panel2.Controls.Add(_stateTextBox);
            _rightSplit.Panel2.Controls.Add(_logTextBox);
            _logTextBox.Dock = DockStyle.Fill;
            _logTextBox.Multiline = true;
            _logTextBox.ScrollBars = ScrollBars.Vertical;
            _stateTextBox.Dock = DockStyle.Top;
            _stateTextBox.Multiline = true;
            _stateTextBox.Height = 180;
            _stateTextBox.ReadOnly = true;
            Controls.Add(_rootSplit);
            Controls.Add(_topStatusLabel);
            Text = "Dummy Client WinForms";
            Width = 1400;
            Height = 880;
            _rootSplit.Panel1.ResumeLayout(false);
            _rootSplit.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(_rootSplit)).EndInit();
            _rootSplit.ResumeLayout(false);
            _leftSplit.Panel1.ResumeLayout(false);
            _leftSplit.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(_leftSplit)).EndInit();
            _leftSplit.ResumeLayout(false);
            _rightSplit.Panel1.ResumeLayout(false);
            _rightSplit.Panel2.ResumeLayout(false);
            _rightSplit.Panel2.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(_rightSplit)).EndInit();
            _rightSplit.ResumeLayout(false);
            ResumeLayout(false);
        }
    }
}
