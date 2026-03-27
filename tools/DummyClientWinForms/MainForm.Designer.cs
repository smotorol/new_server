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
            this._rootSplit = new System.Windows.Forms.SplitContainer();
            this._leftSplit = new System.Windows.Forms.SplitContainer();
            this._loginPanel = new DummyClientWinForms.Scenes.LoginPanel();
            this._characterPanel = new DummyClientWinForms.Scenes.CharacterPanel();
            this._rightSplit = new System.Windows.Forms.SplitContainer();
            this._worldPanel = new DummyClientWinForms.Scenes.WorldPanel();
            this._stateTextBox = new System.Windows.Forms.TextBox();
            this._logTextBox = new System.Windows.Forms.TextBox();
            this._topStatusLabel = new System.Windows.Forms.Label();
            ((System.ComponentModel.ISupportInitialize)(this._rootSplit)).BeginInit();
            this._rootSplit.Panel1.SuspendLayout();
            this._rootSplit.Panel2.SuspendLayout();
            this._rootSplit.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this._leftSplit)).BeginInit();
            this._leftSplit.Panel1.SuspendLayout();
            this._leftSplit.Panel2.SuspendLayout();
            this._leftSplit.SuspendLayout();
            ((System.ComponentModel.ISupportInitialize)(this._rightSplit)).BeginInit();
            this._rightSplit.Panel1.SuspendLayout();
            this._rightSplit.Panel2.SuspendLayout();
            this._rightSplit.SuspendLayout();
            this.SuspendLayout();
            // 
            // _rootSplit
            // 
            this._rootSplit.Dock = System.Windows.Forms.DockStyle.Fill;
            this._rootSplit.Location = new System.Drawing.Point(0, 24);
            this._rootSplit.Name = "_rootSplit";
            // 
            // _rootSplit.Panel1
            // 
            this._rootSplit.Panel1.Controls.Add(this._leftSplit);
            // 
            // _rootSplit.Panel2
            // 
            this._rootSplit.Panel2.Controls.Add(this._rightSplit);
            this._rootSplit.Size = new System.Drawing.Size(1384, 817);
            this._rootSplit.SplitterDistance = 1116;
            this._rootSplit.TabIndex = 0;
            // 
            // _leftSplit
            // 
            this._leftSplit.Dock = System.Windows.Forms.DockStyle.Fill;
            this._leftSplit.Location = new System.Drawing.Point(0, 0);
            this._leftSplit.Name = "_leftSplit";
            this._leftSplit.Orientation = System.Windows.Forms.Orientation.Horizontal;
            // 
            // _leftSplit.Panel1
            // 
            this._leftSplit.Panel1.Controls.Add(this._loginPanel);
            // 
            // _leftSplit.Panel2
            // 
            this._leftSplit.Panel2.Controls.Add(this._characterPanel);
            this._leftSplit.Size = new System.Drawing.Size(1116, 817);
            this._leftSplit.SplitterDistance = 580;
            this._leftSplit.TabIndex = 0;
            // 
            // _loginPanel
            // 
            this._loginPanel.Dock = System.Windows.Forms.DockStyle.Fill;
            this._loginPanel.Location = new System.Drawing.Point(0, 0);
            this._loginPanel.Name = "_loginPanel";
            this._loginPanel.Padding = new System.Windows.Forms.Padding(8);
            this._loginPanel.Size = new System.Drawing.Size(1116, 580);
            this._loginPanel.TabIndex = 0;
            // 
            // _characterPanel
            // 
            this._characterPanel.Dock = System.Windows.Forms.DockStyle.Fill;
            this._characterPanel.Location = new System.Drawing.Point(0, 0);
            this._characterPanel.Name = "_characterPanel";
            this._characterPanel.Padding = new System.Windows.Forms.Padding(8);
            this._characterPanel.Size = new System.Drawing.Size(1116, 233);
            this._characterPanel.TabIndex = 0;
            // 
            // _rightSplit
            // 
            this._rightSplit.Dock = System.Windows.Forms.DockStyle.Fill;
            this._rightSplit.Location = new System.Drawing.Point(0, 0);
            this._rightSplit.Name = "_rightSplit";
            // 
            // _rightSplit.Panel1
            // 
            this._rightSplit.Panel1.Controls.Add(this._worldPanel);
            // 
            // _rightSplit.Panel2
            // 
            this._rightSplit.Panel2.Controls.Add(this._stateTextBox);
            this._rightSplit.Panel2.Controls.Add(this._logTextBox);
            this._rightSplit.Size = new System.Drawing.Size(264, 817);
            this._rightSplit.SplitterDistance = 212;
            this._rightSplit.TabIndex = 0;
            // 
            // _worldPanel
            // 
            this._worldPanel.Dock = System.Windows.Forms.DockStyle.Fill;
            this._worldPanel.Location = new System.Drawing.Point(0, 0);
            this._worldPanel.Name = "_worldPanel";
            this._worldPanel.Padding = new System.Windows.Forms.Padding(8);
            this._worldPanel.Size = new System.Drawing.Size(212, 817);
            this._worldPanel.TabIndex = 0;
            // 
            // _stateTextBox
            // 
            this._stateTextBox.BackColor = System.Drawing.SystemColors.Window;
            this._stateTextBox.Dock = System.Windows.Forms.DockStyle.Top;
            this._stateTextBox.Location = new System.Drawing.Point(0, 0);
            this._stateTextBox.Multiline = true;
            this._stateTextBox.Name = "_stateTextBox";
            this._stateTextBox.ReadOnly = true;
            this._stateTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
            this._stateTextBox.Size = new System.Drawing.Size(48, 262);
            this._stateTextBox.TabIndex = 0;
            // 
            // _logTextBox
            // 
            this._logTextBox.Dock = System.Windows.Forms.DockStyle.Bottom;
            this._logTextBox.Location = new System.Drawing.Point(0, 260);
            this._logTextBox.Multiline = true;
            this._logTextBox.Name = "_logTextBox";
            this._logTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
            this._logTextBox.Size = new System.Drawing.Size(48, 557);
            this._logTextBox.TabIndex = 1;
            // 
            // _topStatusLabel
            // 
            this._topStatusLabel.Dock = System.Windows.Forms.DockStyle.Top;
            this._topStatusLabel.Location = new System.Drawing.Point(0, 0);
            this._topStatusLabel.Name = "_topStatusLabel";
            this._topStatusLabel.Size = new System.Drawing.Size(1384, 24);
            this._topStatusLabel.TabIndex = 1;
            this._topStatusLabel.Text = "Dummy client ready";
            // 
            // MainForm
            // 
            this.ClientSize = new System.Drawing.Size(1384, 841);
            this.Controls.Add(this._rootSplit);
            this.Controls.Add(this._topStatusLabel);
            this.Name = "MainForm";
            this.Text = "Dummy Client WinForms";
            this._rootSplit.Panel1.ResumeLayout(false);
            this._rootSplit.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this._rootSplit)).EndInit();
            this._rootSplit.ResumeLayout(false);
            this._leftSplit.Panel1.ResumeLayout(false);
            this._leftSplit.Panel2.ResumeLayout(false);
            ((System.ComponentModel.ISupportInitialize)(this._leftSplit)).EndInit();
            this._leftSplit.ResumeLayout(false);
            this._rightSplit.Panel1.ResumeLayout(false);
            this._rightSplit.Panel2.ResumeLayout(false);
            this._rightSplit.Panel2.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this._rightSplit)).EndInit();
            this._rightSplit.ResumeLayout(false);
            this.ResumeLayout(false);

        }
    }
}
