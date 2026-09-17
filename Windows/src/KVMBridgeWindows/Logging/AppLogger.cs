using System;
using System.IO;
using System.Runtime.CompilerServices;

namespace KVMBridgeWindows.Logging;

public enum LogLevel { Debug, Info, Warning, Error }

/// <summary>
/// Simple rolling daily log. Thread-safe via lock.
/// </summary>
public sealed class AppLogger : IDisposable
{
    private readonly string _logDir;
    private readonly int _retentionDays;
    private LogLevel _minLevel;
    private StreamWriter? _writer;
    private string _currentFile = string.Empty;
    private readonly object _lock = new();
    private bool _disposed;

    public AppLogger(string logDir, LogLevel minLevel = LogLevel.Info, int retentionDays = 7)
    {
        _logDir = logDir;
        _minLevel = minLevel;
        _retentionDays = retentionDays;
        Directory.CreateDirectory(logDir);
        PurgeOldLogs();
    }

    public void SetLevel(LogLevel level) { lock (_lock) { _minLevel = level; } }

    public void Debug(string msg, [CallerMemberName] string member = "") => Write(LogLevel.Debug, msg, member);
    public void Info(string msg, [CallerMemberName] string member = "") => Write(LogLevel.Info, msg, member);
    public void Warning(string msg, [CallerMemberName] string member = "") => Write(LogLevel.Warning, msg, member);
    public void Error(string msg, [CallerMemberName] string member = "") => Write(LogLevel.Error, msg, member);
    public void Error(string msg, Exception ex, [CallerMemberName] string member = "") =>
        Write(LogLevel.Error, $"{msg} | {ex.GetType().Name}: {ex.Message}", member);

    private void Write(LogLevel level, string msg, string member)
    {
        if (level < _minLevel) return;
        var now = DateTime.Now;
        var line = $"[{now:yyyy-MM-dd HH:mm:ss.fff}] [{level.ToString().ToUpperInvariant(),7}] [{member}] {msg}";
        lock (_lock)
        {
            try
            {
                EnsureWriter(now);
                _writer?.WriteLine(line);
                _writer?.Flush();
            }
            catch { /* log failure must not crash program */ }
        }
    }

    private void EnsureWriter(DateTime now)
    {
        var fileName = Path.Combine(_logDir, $"kvmbridge_{now:yyyyMMdd}.log");
        if (_writer != null && fileName == _currentFile) return;
        _writer?.Dispose();
        _currentFile = fileName;
        _writer = new StreamWriter(fileName, append: true, System.Text.Encoding.UTF8) { AutoFlush = false };
    }

    private void PurgeOldLogs()
    {
        try
        {
            var cutoff = DateTime.Now.AddDays(-_retentionDays);
            foreach (var f in Directory.GetFiles(_logDir, "kvmbridge_*.log"))
                if (File.GetLastWriteTime(f) < cutoff) File.Delete(f);
        }
        catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        lock (_lock) { _writer?.Dispose(); _writer = null; }
    }
}
