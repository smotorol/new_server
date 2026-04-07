using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using DummyClientWinForms.Services;

namespace BenchDummyClient
{
    internal static class BenchRunner
    {
        public static async Task<int> RunAsync(BenchOptions options)
        {
            System.IO.Directory.CreateDirectory(options.ResultDir);
            var accounts = BenchAccountCsv.Load(options.AccountsCsv);
            if (options.AccountStartIndex < 0 || options.AccountStartIndex >= accounts.Count)
            {
                throw new InvalidOperationException("account-start-index is out of range.");
            }
            if (options.AccountStartIndex + options.SessionCount > accounts.Count)
            {
                throw new InvalidOperationException("session-count exceeds available AccountsCsv rows from account-start-index.");
            }

            var selectedAccounts = accounts.Skip(options.AccountStartIndex).Take(options.SessionCount).ToList();
            using (var sink = new BenchResultSink(options.ResultDir, options.RunId, string.IsNullOrWhiteSpace(options.ProcessLabel) ? "bench" : options.ProcessLabel))
            using (var connectSemaphore = new SemaphoreSlim(Math.Max(1, options.MaxConcurrentConnect)))
            {
                sink.Log($"bench start session_count={options.SessionCount} start_index={options.AccountStartIndex} result_dir={options.ResultDir}");
                var gameDataService = new GameDataService();
                var sessions = new List<BenchSession>();
                var tasks = new List<Task>();
                var hardDeadlineSeconds = options.DurationSeconds
                    + (int)Math.Ceiling((Math.Max(0, options.SessionCount - 1) * Math.Max(0, options.LaunchSpacingMs)) / 1000.0)
                    + Math.Max(5, options.ProcessShutdownGraceSeconds);
                using (var cts = new CancellationTokenSource())
                {
                    for (var i = 0; i < selectedAccounts.Count; i++)
                    {
                        var session = new BenchSession(options, selectedAccounts[i], i + 1, sink, connectSemaphore, gameDataService);
                        sessions.Add(session);
                        tasks.Add(session.RunAsync(cts.Token));
                        if (options.LaunchSpacingMs > 0)
                        {
                            await Task.Delay(options.LaunchSpacingMs).ConfigureAwait(false);
                        }
                    }

                    var allTasks = Task.WhenAll(tasks);
                    var completed = await Task.WhenAny(allTasks, Task.Delay(TimeSpan.FromSeconds(hardDeadlineSeconds))).ConfigureAwait(false);
                    if (!ReferenceEquals(completed, allTasks))
                    {
                        sink.Log($"bench hard deadline reached. hard_deadline_seconds={hardDeadlineSeconds} pending_sessions={tasks.Count(t => !t.IsCompleted)}");
                        cts.Cancel();
                        foreach (var session in sessions)
                        {
                            session.Dispose();
                        }
                        await Task.WhenAny(allTasks, Task.Delay(TimeSpan.FromSeconds(5))).ConfigureAwait(false);
                    }
                    else
                    {
                        try
                        {
                            await allTasks.ConfigureAwait(false);
                        }
                        finally
                        {
                            foreach (var session in sessions)
                            {
                                session.Dispose();
                            }
                        }
                    }
                }

                var summary = sink.WriteSummary(options.SessionCount);
                sink.Log($"bench complete success={summary.success_count} failure={summary.failure_count}");
                Console.WriteLine("Bench complete. ResultDir={0}", options.ResultDir);
                return summary.failure_count > 0 ? 2 : 0;
            }
        }
    }
}

