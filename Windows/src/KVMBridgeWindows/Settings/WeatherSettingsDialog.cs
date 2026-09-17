using KVMBridgeWindows.Configuration;

namespace KVMBridgeWindows.Settings;

public sealed class WeatherSettingsDialog : Form
{
    private readonly ComboBox _provider = new() { DropDownStyle = ComboBoxStyle.DropDownList };
    private readonly TextBox _city = new();
    private readonly TextBox _host = new();
    private readonly TextBox _key = new() { UseSystemPasswordChar = true };
    private readonly Label _cityHint = new() { AutoSize = true };
    private readonly Label _hostLabel = new() { Text = "QWeather API Host", AutoSize = true, Anchor = AnchorStyles.Left };
    private readonly Label _keyLabel = new() { Text = "API Key（留空则保持原值）", AutoSize = true, Anchor = AnchorStyles.Left };

    public DashboardSettingsUpdate SettingsUpdate => new(
        _provider.SelectedItem?.ToString() ?? "open_meteo",
        _city.Text.Trim(),
        _host.Text.Trim(),
        _key.Text);

    public WeatherSettingsDialog(DashboardConfig config)
    {
        Text = "天气数据设置";
        FormBorderStyle = FormBorderStyle.FixedDialog;
        MaximizeBox = false;
        MinimizeBox = false;
        StartPosition = FormStartPosition.CenterScreen;
        ClientSize = new Size(520, 292);
        ShowInTaskbar = false;

        _provider.Items.AddRange(["open_meteo", "qweather", "amap"]);
        _provider.SelectedItem = config.WeatherProvider;
        if (_provider.SelectedIndex < 0) _provider.SelectedIndex = 0;
        _city.Text = string.IsNullOrWhiteSpace(config.CityCode) ? config.CityName : config.CityCode;
        _host.Text = config.QWeatherApiHost;

        var current = string.IsNullOrWhiteSpace(config.CityName)
            ? "当前城市：未配置"
            : $"当前城市：{config.CityName}（{config.CityCode}）";
        var table = new TableLayoutPanel
        {
            Dock = DockStyle.Fill,
            Padding = new Padding(16),
            ColumnCount = 2,
            RowCount = 7
        };
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 180));
        table.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        table.Controls.Add(new Label { Text = current, AutoSize = true }, 0, 0);
        table.SetColumnSpan(table.GetControlFromPosition(0, 0)!, 2);
        table.Controls.Add(new Label { Text = "天气源", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 1);
        table.Controls.Add(_provider, 1, 1);
        table.Controls.Add(new Label { Text = "城市代码或名称", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 2);
        table.Controls.Add(_city, 1, 2);
        table.Controls.Add(_cityHint, 1, 3);
        table.Controls.Add(_hostLabel, 0, 4);
        table.Controls.Add(_host, 1, 4);
        table.Controls.Add(_keyLabel, 0, 5);
        table.Controls.Add(_key, 1, 5);

        var buttons = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
        var cancel = new Button { Text = "取消", DialogResult = DialogResult.Cancel, AutoSize = true };
        var save = new Button { Text = "保存并刷新", DialogResult = DialogResult.OK, AutoSize = true };
        buttons.Controls.Add(cancel);
        buttons.Controls.Add(save);
        table.Controls.Add(buttons, 0, 6);
        table.SetColumnSpan(buttons, 2);
        Controls.Add(table);
        AcceptButton = save;
        CancelButton = cancel;

        _provider.SelectedIndexChanged += (_, _) => UpdateProviderFields();
        UpdateProviderFields();
    }

    private void UpdateProviderFields()
    {
        var provider = _provider.SelectedItem?.ToString();
        _host.Enabled = provider == "qweather";
        _hostLabel.Enabled = _host.Enabled;
        _key.Enabled = provider is "qweather" or "amap";
        _keyLabel.Enabled = _key.Enabled;
        _cityHint.Text = provider switch
        {
            "amap" => "高德地图要求填写 6 位 adcode。",
            "qweather" => "可填写和风天气 Location ID 或城市名称。",
            _ => "可填写 GeoNames 数字 ID 或中国城市名称。"
        };
    }
}
