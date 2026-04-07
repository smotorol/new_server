using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Web.Script.Serialization;

namespace BenchDummyClient
{
    internal sealed class BenchResultSink : IDisposable
    {
        private readonly object _sync = new object();
        private readonly JavaScriptSerializer _serializer = new JavaScriptSerializer { MaxJsonLength = int.MaxValue, RecursionLimit = 256 };
        private readonly StreamWriter _resultWriter;
        private readonly StreamWriter _processLogWriter;
        private readonly List<BenchSessionResult> _results = new List<BenchSessionResult>();
        private readonly string _resultsPath;
        private readonly string _summaryPath;
        private readonly string _failuresPath;
        private readonly string _processLogPath;
        private readonly string _runId;
        private readonly string _processLabel;
        private readonly DateTime _startedAtUtc;

        public BenchResultSink(string resultDir, string runId, string processLabel)
        {
            Directory.CreateDirectory(resultDir);
            _runId = runId;
            _processLabel = processLabel;
            _startedAtUtc = DateTime.UtcNow;
            _resultsPath = Path.Combine(resultDir, "bench_results.jsonl");
            _summaryPath = Path.Combine(resultDir, "bench_summary.json");
            _failuresPath = Path.Combine(resultDir, "bench_failures.json");
            _processLogPath = Path.Combine(resultDir, "bench_process.log");
            _resultWriter = new StreamWriter(new FileStream(_resultsPath, FileMode.Create, FileAccess.Write, FileShare.Read)) { AutoFlush = true };
            _processLogWriter = new StreamWriter(new FileStream(_processLogPath, FileMode.Create, FileAccess.Write, FileShare.Read)) { AutoFlush = true };
        }

        public string ResultsPath => _resultsPath;
        public string SummaryPath => _summaryPath;
        public string FailuresPath => _failuresPath;
        public string ProcessLogPath => _processLogPath;

        public void Log(string message)
        {
            lock (_sync)
            {
                _processLogWriter.WriteLine("[{0}] {1}", DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss", CultureInfo.InvariantCulture), message);
            }
        }

        public void RecordResult(BenchSessionResult result)
        {
            lock (_sync)
            {
                _results.Add(result);
                _resultWriter.WriteLine(_serializer.Serialize(result));
            }
        }

        public BenchSummary WriteSummary(int requestedSessions)
        {
            lock (_sync)
            {
                var durations = _results.Select(x => GetDurationSeconds(x.started_at_utc, x.ended_at_utc)).Where(x => x.HasValue).Select(x => x.Value).ToList();
                var summary = new BenchSummary
                {
                    run_id = _runId,
                    process_label = _processLabel,
                    process_id = System.Diagnostics.Process.GetCurrentProcess().Id,
                    started_at_utc = _startedAtUtc.ToString("o", CultureInfo.InvariantCulture),
                    finished_at_utc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
                    requested_sessions = requestedSessions,
                    launched_sessions = _results.Count,
                    success_count = _results.Count(x => x.success),
                    failure_count = _results.Count(x => !x.success),
                    login_success_count = _results.Count(x => x.login_success),
                    enter_world_success_count = _results.Count(x => x.enter_world_success),
                    reconnect_success_count = _results.Count(x => x.reconnect_success),
                    disconnected_count = _results.Count(x => x.disconnected),
                    timeout_count = _results.Count(x => x.timeout),
                    average_duration_seconds = durations.Count > 0 ? Math.Round(durations.Average(), 3) : 0,
                    final_status_counts = _results.GroupBy(x => x.final_status ?? string.Empty).OrderBy(x => x.Key).Select(x => new Dictionary<string, object> { ["status"] = x.Key, ["count"] = x.Count() }).ToList(),
                    failure_reason_counts = _results.Where(x => !string.IsNullOrWhiteSpace(x.failure_reason)).GroupBy(x => x.failure_reason).OrderBy(x => x.Key).Select(x => new Dictionary<string, object> { ["reason"] = x.Key, ["count"] = x.Count() }).ToList(),
                    results_jsonl = _resultsPath,
                    failures_json = _failuresPath,
                    process_log = _processLogPath,
                };
                File.WriteAllText(_summaryPath, _serializer.Serialize(summary));
                File.WriteAllText(_failuresPath, _serializer.Serialize(_results.Where(x => !x.success).ToList()));
                return summary;
            }
        }

        private static double? GetDurationSeconds(string startedAt, string endedAt)
        {
            if (string.IsNullOrWhiteSpace(startedAt) || string.IsNullOrWhiteSpace(endedAt))
            {
                return null;
            }
            if (!DateTime.TryParse(startedAt, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var started))
            {
                return null;
            }
            if (!DateTime.TryParse(endedAt, CultureInfo.InvariantCulture, DateTimeStyles.RoundtripKind, out var ended))
            {
                return null;
            }
            return (ended - started).TotalSeconds;
        }

        public void Dispose()
        {
            lock (_sync)
            {
                _resultWriter?.Dispose();
                _processLogWriter?.Dispose();
            }
        }
    }
}
