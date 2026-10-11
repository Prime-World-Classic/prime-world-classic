namespace EditorPlugins.Editors.TextRef
{
	partial class TextRefEditorForm
	{
		/// <summary>
		/// Required designer variable.
		/// </summary>
		private System.ComponentModel.IContainer components = null;

		/// <summary>
		/// Clean up any resources being used.
		/// </summary>
		/// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
		protected override void Dispose( bool disposing )
		{
			if ( disposing && ( components != null ) )
			{
				components.Dispose();
			}
			base.Dispose( disposing );
		}

		#region Windows Form Designer generated code

		/// <summary>
		/// Required method for Designer support - do not modify
		/// the contents of this method with the code editor.
		/// </summary>
		private void InitializeComponent()
		{
            this.btnOk = new System.Windows.Forms.Button();
            this.btnCancel = new System.Windows.Forms.Button();
            this.cbWordWrap = new System.Windows.Forms.CheckBox();
            this.tbMain = new ScintillaNET.Scintilla();
            this.tbEn = new ScintillaNET.Scintilla();
            this.splitTexts = new System.Windows.Forms.SplitContainer();
            this.lblMain = new System.Windows.Forms.Label();
            this.lblEn = new System.Windows.Forms.Label();
            ((System.ComponentModel.ISupportInitialize)(this.tbMain)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.tbEn)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.splitTexts)).BeginInit();
            this.splitTexts.Panel1.SuspendLayout();
            this.splitTexts.Panel2.SuspendLayout();
            this.splitTexts.SuspendLayout();
            this.SuspendLayout();
            // 
            // btnOk
            // 
            this.btnOk.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
            this.btnOk.Location = new System.Drawing.Point(272, 309);
            this.btnOk.Name = "btnOk";
            this.btnOk.Size = new System.Drawing.Size(75, 23);
            this.btnOk.TabIndex = 1;
            this.btnOk.Text = "Ok";
            this.btnOk.UseVisualStyleBackColor = true;
            this.btnOk.Click += new System.EventHandler(this.btnOk_Click);
            // 
            // btnCancel
            // 
            this.btnCancel.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Right)));
            this.btnCancel.DialogResult = System.Windows.Forms.DialogResult.Cancel;
            this.btnCancel.Location = new System.Drawing.Point(353, 309);
            this.btnCancel.Name = "btnCancel";
            this.btnCancel.Size = new System.Drawing.Size(75, 23);
            this.btnCancel.TabIndex = 2;
            this.btnCancel.Text = "Cancel";
            this.btnCancel.UseVisualStyleBackColor = true;
            this.btnCancel.Click += new System.EventHandler(this.btnCancel_Click);
            // 
            // cbWordWrap
            // 
            this.cbWordWrap.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left)));
            this.cbWordWrap.AutoSize = true;
            this.cbWordWrap.Checked = true;
            this.cbWordWrap.CheckState = System.Windows.Forms.CheckState.Checked;
            this.cbWordWrap.Location = new System.Drawing.Point(12, 314);
            this.cbWordWrap.Name = "cbWordWrap";
            this.cbWordWrap.Size = new System.Drawing.Size(81, 17);
            this.cbWordWrap.TabIndex = 3;
            this.cbWordWrap.Text = "Word Wrap";
            this.cbWordWrap.UseVisualStyleBackColor = true;
            this.cbWordWrap.CheckedChanged += new System.EventHandler(this.cbWrap_CheckedChanged);
            // 
            // splitTexts
            //
            this.splitTexts.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
                        | System.Windows.Forms.AnchorStyles.Left)
                        | System.Windows.Forms.AnchorStyles.Right)));
            this.splitTexts.Location = new System.Drawing.Point(12, 12);
            this.splitTexts.Name = "splitTexts";
            this.splitTexts.Orientation = System.Windows.Forms.Orientation.Horizontal;
            //
            // splitTexts.Panel1
            //
            this.splitTexts.Panel1.Controls.Add(this.tbMain);
            this.splitTexts.Panel1.Controls.Add(this.lblMain);
            //
            // splitTexts.Panel2
            //
            this.splitTexts.Panel2.Controls.Add(this.tbEn);
            this.splitTexts.Panel2.Controls.Add(this.lblEn);
            this.splitTexts.Size = new System.Drawing.Size(416, 291);
            this.splitTexts.SplitterDistance = 143;
            this.splitTexts.TabIndex = 4;
            //
            // lblMain
            //
            this.lblMain.AutoSize = true;
            this.lblMain.Dock = System.Windows.Forms.DockStyle.Top;
            this.lblMain.Location = new System.Drawing.Point(0, 0);
            this.lblMain.Name = "lblMain";
            this.lblMain.Padding = new System.Windows.Forms.Padding(0, 0, 0, 3);
            this.lblMain.Size = new System.Drawing.Size(42, 16);
            this.lblMain.TabIndex = 0;
            this.lblMain.Text = "Original";
            //
            // tbMain
            //
            this.tbMain.ConfigurationManager.Language = "xml";
            this.tbMain.Dock = System.Windows.Forms.DockStyle.Fill;
            this.tbMain.LineWrapping.Mode = ScintillaNET.LineWrappingMode.Word;
            this.tbMain.Location = new System.Drawing.Point(0, 16);
            this.tbMain.Name = "tbMain";
            this.tbMain.Size = new System.Drawing.Size(416, 127);
            this.tbMain.TabIndex = 1;
            //
            // lblEn
            //
            this.lblEn.AutoSize = true;
            this.lblEn.Dock = System.Windows.Forms.DockStyle.Top;
            this.lblEn.Location = new System.Drawing.Point(0, 0);
            this.lblEn.Name = "lblEn";
            this.lblEn.Padding = new System.Windows.Forms.Padding(0, 0, 0, 3);
            this.lblEn.Size = new System.Drawing.Size(97, 16);
            this.lblEn.TabIndex = 0;
            this.lblEn.Text = "English (_en.txt)";
            //
            // tbEn
            //
            this.tbEn.ConfigurationManager.Language = "xml";
            this.tbEn.Dock = System.Windows.Forms.DockStyle.Fill;
            this.tbEn.LineWrapping.Mode = ScintillaNET.LineWrappingMode.Word;
            this.tbEn.Location = new System.Drawing.Point(0, 16);
            this.tbEn.Name = "tbEn";
            this.tbEn.Size = new System.Drawing.Size(416, 128);
            this.tbEn.TabIndex = 1;
            //
            // TextRefEditorForm
            //
            this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(440, 339);
            this.Controls.Add(this.splitTexts);
            this.Controls.Add(this.cbWordWrap);
            this.Controls.Add(this.btnCancel);
            this.Controls.Add(this.btnOk);
            this.Name = "TextRefEditorForm";
            this.ShowIcon = false;
            this.ShowInTaskbar = false;
            this.SizeGripStyle = System.Windows.Forms.SizeGripStyle.Show;
            this.Text = "TextRefEditorForm";
            this.Load += new System.EventHandler(this.TextRefEditorForm_Load);
            this.FormClosing += new System.Windows.Forms.FormClosingEventHandler(this.TextRefEditorForm_FormClosing);
            ((System.ComponentModel.ISupportInitialize)(this.tbMain)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.tbEn)).EndInit();
            this.splitTexts.Panel1.ResumeLayout(false);
            this.splitTexts.Panel1.PerformLayout();
            this.splitTexts.Panel2.ResumeLayout(false);
            this.splitTexts.Panel2.PerformLayout();
            ((System.ComponentModel.ISupportInitialize)(this.splitTexts)).EndInit();
            this.splitTexts.ResumeLayout(false);
            this.ResumeLayout(false);
            this.PerformLayout();

		}

		#endregion

    private System.Windows.Forms.Button btnOk;
		private System.Windows.Forms.Button btnCancel;
		private System.Windows.Forms.CheckBox cbWordWrap;
    private ScintillaNET.Scintilla tbMain;
    private ScintillaNET.Scintilla tbEn;
    private System.Windows.Forms.SplitContainer splitTexts;
    private System.Windows.Forms.Label lblMain;
    private System.Windows.Forms.Label lblEn;
	}
}