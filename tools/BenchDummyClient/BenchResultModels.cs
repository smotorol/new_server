using System.Collections.Generic;

namespace BenchDummyClient
{
    internal enum BenchSessionStage
    {
        Created,
        ConnectingLogin,
        LoginSent,
        LoginSuccess,
        WorldSelected,
        CharacterListReceived,
        CharacterSelected,
        ConnectingWorld,
        EnterWorldPending,
        EnterWorldSuccess,
        Moving,
        PortalRequested,
        Reconnecting,
        Completed,
        Failed,
    }

    internal sealed class BenchSessionEvent
    {
        public string timestamp_utc { get; set; }
        public string status { get; set; }
        public string detail { get; set; }
    }

    internal sealed class BenchSessionResult
    {
        public string run_id { get; set; }
        public string process_label { get; set; }
        public int process_id { get; set; }
        public int session_index { get; set; }
        public string client_tag { get; set; }
        public string login_id { get; set; }
        public string final_status { get; set; }
        public bool success { get; set; }
        public string failure_reason { get; set; }
        public bool login_success { get; set; }
        public bool world_selected { get; set; }
        public bool character_selected { get; set; }
        public bool enter_world_success { get; set; }
        public bool reconnect_success { get; set; }
        public bool disconnected { get; set; }
        public bool timeout { get; set; }
        public ulong account_id { get; set; }
        public ulong char_id { get; set; }
        public int zone_id { get; set; }
        public int map_id { get; set; }
        public int pos_x { get; set; }
        public int pos_y { get; set; }
        public int reconnect_attempts { get; set; }
        public int disconnect_count { get; set; }
        public int move_attempts { get; set; }
        public int portal_attempts { get; set; }
        public string started_at_utc { get; set; }
        public string updated_at_utc { get; set; }
        public string ended_at_utc { get; set; }
        public List<BenchSessionEvent> events { get; } = new List<BenchSessionEvent>();
    }

    internal sealed class BenchSummary
    {
        public string run_id { get; set; }
        public string process_label { get; set; }
        public int process_id { get; set; }
        public string started_at_utc { get; set; }
        public string finished_at_utc { get; set; }
        public int requested_sessions { get; set; }
        public int launched_sessions { get; set; }
        public int success_count { get; set; }
        public int failure_count { get; set; }
        public int login_success_count { get; set; }
        public int enter_world_success_count { get; set; }
        public int reconnect_success_count { get; set; }
        public int disconnected_count { get; set; }
        public int timeout_count { get; set; }
        public double average_duration_seconds { get; set; }
        public List<Dictionary<string, object>> final_status_counts { get; set; }
        public List<Dictionary<string, object>> failure_reason_counts { get; set; }
        public string results_jsonl { get; set; }
        public string failures_json { get; set; }
        public string process_log { get; set; }
    }
}
