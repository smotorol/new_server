using System;

namespace BenchDummyClient
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            try
            {
                var options = BenchOptions.Parse(args);
                return BenchRunner.RunAsync(options).GetAwaiter().GetResult();
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("bench fatal: " + ex);
                return 10;
            }
        }
    }
}
