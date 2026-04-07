using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;
using DummyClientWinForms.Services;

namespace DummyClientWinForms
{
    public partial class MainForm
    {
        private sealed class AutomationStatusEntry
        {
            public string TimestampUtc { get; set; }
            public string Status { get; set; }
            public string Detail { get; set; }
        }

        private sealed class AutomationClientResultState
        {
            public string RunId { get; set; }
            public int ClientIndex { get; set; }
            public string ClientTag { get; set; }
            public string LoginId { get; set; }
            public bool Headless { get; set; }
            public string StartedAtUtc { get; set; }
            public string UpdatedAtUtc { get; set; }
            public string EndedAtUtc { get; set; }
            public string FinalStatus { get; set; }
            public bool Success { get; set; }
            public string FailureReason { get; set; }
            public bool LoginSuccess { get; set; }
            public bool WorldSelected { get; set; }
            public bool CharacterSelected { get; set; }
            public bool EnterWorldSuccess { get; set; }
            public bool ReconnectSuccess { get; set; }
            public bool Disconnected { get; set; }
            public bool Timeout { get; set; }
            public int ReconnectAttempts { get; set; }
            public int DisconnectCount { get; set; }
            public List<AutomationStatusEntry> Events { get; } = new List<AutomationStatusEntry>();
        }

        private readonly object _automationResultLock = new object();
        private string _automationResultPath = string.Empty;
        private AutomationClientResultState _automationResult;
        private bool _automationEnteredWorldOnce;

        private void InitializeAutomationResultTracking()
        {
            if (!IsAutomationEnabled)
            {
                return;
            }

            _automationResultPath = ResolveAutomationResultPath();
            _automationResult = new AutomationClientResultState
            {
                RunId = _automationOptions.RunId ?? string.Empty,
                ClientIndex = _automationOptions.ClientIndex,
                ClientTag = _automationOptions.ClientTag ?? string.Empty,
                LoginId = _automationOptions.LoginId ?? string.Empty,
                Headless = _automationOptions.Headless,
                StartedAtUtc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
                UpdatedAtUtc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
                FinalStatus = "configured",
                FailureReason = string.Empty,
            };
            WriteAutomationResultFile();
        }

        private string ResolveAutomationResultPath()
        {
            if (!string.IsNullOrWhiteSpace(_automationOptions.ClientResultPath))
            {
                return _automationOptions.ClientResultPath;
            }

            var baseDir = !string.IsNullOrWhiteSpace(_automationOptions.ClientLogPath)
                ? Path.GetDirectoryName(_automationOptions.ClientLogPath)
                : AppDomain.CurrentDomain.BaseDirectory;
            var fileName = string.Format(CultureInfo.InvariantCulture, "client_result_{0:D5}.json", Math.Max(1, _automationOptions.ClientIndex));
            return Path.Combine(baseDir ?? AppDomain.CurrentDomain.BaseDirectory, fileName);
        }

        private void RecordAutomationStatus(string status, string detail)
        {
            if (!IsAutomationEnabled || _automationResult == null)
            {
                return;
            }

            lock (_automationResultLock)
            {
                _automationResult.FinalStatus = status;
                _automationResult.UpdatedAtUtc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
                _automationResult.Events.Add(new AutomationStatusEntry
                {
                    TimestampUtc = _automationResult.UpdatedAtUtc,
                    Status = status,
                    Detail = detail ?? string.Empty,
                });
                if (_automationResult.Events.Count > 128)
                {
                    _automationResult.Events.RemoveAt(0);
                }
                WriteAutomationResultFile_NoLock();
            }
        }

        private void FinalizeAutomationResult(bool success, string failureReason)
        {
            if (!IsAutomationEnabled || _automationResult == null)
            {
                return;
            }

            lock (_automationResultLock)
            {
                _automationResult.Success = success;
                if (!success && !string.IsNullOrWhiteSpace(failureReason))
                {
                    _automationResult.FailureReason = failureReason;
                }
                if (string.IsNullOrWhiteSpace(_automationResult.EndedAtUtc))
                {
                    _automationResult.EndedAtUtc = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture);
                }
                _automationResult.UpdatedAtUtc = _automationResult.EndedAtUtc;
                WriteAutomationResultFile_NoLock();
            }
        }

        private void AutomationMarkLoginResult(bool ok, string reason)
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.LoginSuccess = ok;
            RecordAutomationStatus(ok ? "login_success" : "login_failed", NormalizeAutomationDetail(reason));
            if (!ok)
            {
                FinalizeAutomationResult(false, string.IsNullOrWhiteSpace(reason) ? "login_failed" : reason);
            }
        }

        private void AutomationMarkWorldSelected(bool ok, int worldId, string reason)
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.WorldSelected = ok;
            RecordAutomationStatus(ok ? "world_selected" : "world_select_failed", $"world_id={worldId} {NormalizeAutomationDetail(reason)}".Trim());
        }

        private void AutomationMarkCharacterList(bool ok, int count, string reason)
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            var status = ok ? (count > 0 ? "character_list_loaded" : "character_list_empty") : "character_list_failed";
            RecordAutomationStatus(status, $"count={count} {NormalizeAutomationDetail(reason)}".Trim());
            if (ok && count == 0)
            {
                FinalizeAutomationResult(false, "character_list_empty");
            }
        }

        private void AutomationMarkCharacterSelected(bool ok, ulong charId, string reason)
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.CharacterSelected = ok;
            RecordAutomationStatus(ok ? "character_selected" : "character_select_failed", $"char_id={charId} {NormalizeAutomationDetail(reason)}".Trim());
        }

        private void AutomationMarkEnterWorldResult(bool ok, string reason, ulong charId)
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.EnterWorldSuccess = ok;
            if (ok)
            {
                _automationEnteredWorldOnce = true;
            }
            RecordAutomationStatus(ok ? "enter_world_success" : "enter_world_failed", $"char_id={charId} {NormalizeAutomationDetail(reason)}".Trim());
            if (!ok)
            {
                FinalizeAutomationResult(false, string.IsNullOrWhiteSpace(reason) ? "enter_world_failed" : reason);
            }
        }

        private void AutomationMarkReconnectResult(bool ok, string reason, ulong charId)
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.ReconnectSuccess = ok;
            RecordAutomationStatus(ok ? "reconnect_success" : "reconnect_failed", $"char_id={charId} {NormalizeAutomationDetail(reason)}".Trim());
            if (!ok)
            {
                FinalizeAutomationResult(false, string.IsNullOrWhiteSpace(reason) ? "reconnect_failed" : reason);
            }
        }

        private void AutomationMarkDisconnected(bool isWorld, DisconnectReason reason, string rawReason, bool expected)
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.Disconnected = true;
            _automationResult.DisconnectCount++;
            var scope = isWorld ? "world" : "login";
            RecordAutomationStatus("disconnected", $"scope={scope} reason={reason} expected={(expected ? 1 : 0)} raw={NormalizeAutomationDetail(rawReason)}");
            if (!expected && !_automationShutdownRequested)
            {
                FinalizeAutomationResult(_automationEnteredWorldOnce, $"{scope}_disconnected_{reason}");
            }
        }

        private void AutomationMarkReconnectRequested()
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.ReconnectAttempts++;
            RecordAutomationStatus("reconnect_requested", "automation reconnect trigger");
        }

        private void AutomationMarkTimeoutReached()
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            _automationResult.Timeout = true;
            RecordAutomationStatus("timeout", "duration reached");
        }

        private void AutomationMarkCompleted()
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            RecordAutomationStatus(_automationResult.Timeout ? "timeout" : "completed", _automationEnteredWorldOnce ? "automation finished" : "automation finished before enter world");
            FinalizeAutomationResult(_automationEnteredWorldOnce, _automationEnteredWorldOnce ? string.Empty : (_automationResult.Timeout ? "timeout_before_enter_world" : "enter_world_not_completed"));
        }

        private void AutomationMarkProcessClosed()
        {
            if (!IsAutomationEnabled)
            {
                return;
            }
            RecordAutomationStatus("process_closed", "form closed before automation shutdown");
            FinalizeAutomationResult(false, _automationEnteredWorldOnce ? string.Empty : "process_closed");
        }

        private void WriteAutomationResultFile()
        {
            if (!IsAutomationEnabled || _automationResult == null)
            {
                return;
            }

            lock (_automationResultLock)
            {
                WriteAutomationResultFile_NoLock();
            }
        }

        private void WriteAutomationResultFile_NoLock()
        {
            if (string.IsNullOrWhiteSpace(_automationResultPath) || _automationResult == null)
            {
                return;
            }

            var directory = Path.GetDirectoryName(_automationResultPath);
            if (!string.IsNullOrWhiteSpace(directory))
            {
                Directory.CreateDirectory(directory);
            }
            File.WriteAllText(_automationResultPath, BuildAutomationResultJson(_automationResult), Encoding.UTF8);
        }

        private static string BuildAutomationResultJson(AutomationClientResultState state)
        {
            var sb = new StringBuilder();
            sb.AppendLine("{");
            AppendAutomationJsonProperty(sb, "run_id", state.RunId, true, 2);
            AppendAutomationJsonProperty(sb, "client_index", state.ClientIndex, true, 2);
            AppendAutomationJsonProperty(sb, "client_tag", state.ClientTag, true, 2);
            AppendAutomationJsonProperty(sb, "login_id", state.LoginId, true, 2);
            AppendAutomationJsonProperty(sb, "headless", state.Headless, true, 2);
            AppendAutomationJsonProperty(sb, "started_at_utc", state.StartedAtUtc, true, 2);
            AppendAutomationJsonProperty(sb, "updated_at_utc", state.UpdatedAtUtc, true, 2);
            AppendAutomationJsonProperty(sb, "ended_at_utc", state.EndedAtUtc, true, 2);
            AppendAutomationJsonProperty(sb, "final_status", state.FinalStatus, true, 2);
            AppendAutomationJsonProperty(sb, "success", state.Success, true, 2);
            AppendAutomationJsonProperty(sb, "failure_reason", state.FailureReason, true, 2);
            AppendAutomationJsonProperty(sb, "login_success", state.LoginSuccess, true, 2);
            AppendAutomationJsonProperty(sb, "world_selected", state.WorldSelected, true, 2);
            AppendAutomationJsonProperty(sb, "character_selected", state.CharacterSelected, true, 2);
            AppendAutomationJsonProperty(sb, "enter_world_success", state.EnterWorldSuccess, true, 2);
            AppendAutomationJsonProperty(sb, "reconnect_success", state.ReconnectSuccess, true, 2);
            AppendAutomationJsonProperty(sb, "disconnected", state.Disconnected, true, 2);
            AppendAutomationJsonProperty(sb, "timeout", state.Timeout, true, 2);
            AppendAutomationJsonProperty(sb, "reconnect_attempts", state.ReconnectAttempts, true, 2);
            AppendAutomationJsonProperty(sb, "disconnect_count", state.DisconnectCount, true, 2);
            sb.AppendLine("  \"events\": [");
            for (var i = 0; i < state.Events.Count; i++)
            {
                var entry = state.Events[i];
                sb.AppendLine("    {");
                AppendAutomationJsonProperty(sb, "timestamp_utc", entry.TimestampUtc, true, 6);
                AppendAutomationJsonProperty(sb, "status", entry.Status, true, 6);
                AppendAutomationJsonProperty(sb, "detail", entry.Detail, false, 6);
                sb.Append("    }");
                if (i + 1 < state.Events.Count)
                {
                    sb.Append(',');
                }
                sb.AppendLine();
            }
            sb.AppendLine("  ]");
            sb.AppendLine("}");
            return sb.ToString();
        }

        private static void AppendAutomationJsonProperty(StringBuilder sb, string name, string value, bool comma, int indent)
        {
            sb.Append(' ', indent);
            sb.Append('"').Append(EscapeAutomationJson(name)).Append("\": ");
            if (value == null)
            {
                sb.Append("null");
            }
            else
            {
                sb.Append('"').Append(EscapeAutomationJson(value)).Append('"');
            }
            if (comma)
            {
                sb.Append(',');
            }
            sb.AppendLine();
        }

        private static void AppendAutomationJsonProperty(StringBuilder sb, string name, int value, bool comma, int indent)
        {
            sb.Append(' ', indent);
            sb.Append('"').Append(EscapeAutomationJson(name)).Append("\": ").Append(value.ToString(CultureInfo.InvariantCulture));
            if (comma)
            {
                sb.Append(',');
            }
            sb.AppendLine();
        }

        private static void AppendAutomationJsonProperty(StringBuilder sb, string name, bool value, bool comma, int indent)
        {
            sb.Append(' ', indent);
            sb.Append('"').Append(EscapeAutomationJson(name)).Append("\": ").Append(value ? "true" : "false");
            if (comma)
            {
                sb.Append(',');
            }
            sb.AppendLine();
        }

        private static string EscapeAutomationJson(string value)
        {
            if (string.IsNullOrEmpty(value))
            {
                return value ?? string.Empty;
            }

            return value
                .Replace("\\", "\\\\")
                .Replace("\"", "\\\"")
                .Replace("\r", "\\r")
                .Replace("\n", "\\n")
                .Replace("\t", "\\t");
        }

        private static string NormalizeAutomationDetail(string value)
        {
            return string.IsNullOrWhiteSpace(value) ? string.Empty : value.Trim();
        }
    }
}
