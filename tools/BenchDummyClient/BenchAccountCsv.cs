using System;
using System.Collections.Generic;
using Microsoft.VisualBasic.FileIO;

namespace BenchDummyClient
{
    internal sealed class BenchAccountEntry
    {
        public string LoginId { get; set; } = string.Empty;
        public string Password { get; set; } = string.Empty;
        public int WorldIndex { get; set; }
        public int CharacterIndex { get; set; }
        public string PortalRoute { get; set; } = string.Empty;
        public string ClientTag { get; set; } = string.Empty;
    }

    internal static class BenchAccountCsv
    {
        public static IReadOnlyList<BenchAccountEntry> Load(string path)
        {
            var rows = new List<BenchAccountEntry>();
            using (var parser = new TextFieldParser(path))
            {
                parser.TextFieldType = FieldType.Delimited;
                parser.SetDelimiters(",");
                parser.HasFieldsEnclosedInQuotes = true;
                if (parser.EndOfData)
                {
                    throw new InvalidOperationException("AccountsCsv is empty.");
                }

                var headers = parser.ReadFields();
                if (headers == null)
                {
                    throw new InvalidOperationException("AccountsCsv header row missing.");
                }
                var columnMap = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);
                for (var i = 0; i < headers.Length; i++)
                {
                    columnMap[headers[i] ?? string.Empty] = i;
                }

                RequireColumn(columnMap, "LoginId");
                RequireColumn(columnMap, "Password");

                while (!parser.EndOfData)
                {
                    var fields = parser.ReadFields();
                    if (fields == null || fields.Length == 0)
                    {
                        continue;
                    }
                    var entry = new BenchAccountEntry
                    {
                        LoginId = ReadField(columnMap, fields, "LoginId"),
                        Password = ReadField(columnMap, fields, "Password"),
                        WorldIndex = ParseInt(ReadField(columnMap, fields, "WorldIndex"), 0),
                        CharacterIndex = ParseInt(ReadField(columnMap, fields, "CharacterIndex"), 0),
                        PortalRoute = ReadField(columnMap, fields, "PortalRoute"),
                        ClientTag = ReadField(columnMap, fields, "ClientTag"),
                    };
                    if (!string.IsNullOrWhiteSpace(entry.LoginId))
                    {
                        rows.Add(entry);
                    }
                }
            }
            return rows;
        }

        private static void RequireColumn(Dictionary<string, int> map, string name)
        {
            if (!map.ContainsKey(name))
            {
                throw new InvalidOperationException("AccountsCsv missing required column: " + name);
            }
        }

        private static string ReadField(Dictionary<string, int> map, string[] fields, string name)
        {
            if (!map.TryGetValue(name, out var index))
            {
                return string.Empty;
            }
            return index >= 0 && index < fields.Length ? (fields[index] ?? string.Empty) : string.Empty;
        }

        private static int ParseInt(string value, int fallback)
        {
            return int.TryParse(value, out var parsed) ? parsed : fallback;
        }
    }
}
