using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading.Tasks;
using Xunit;

namespace OpenCKey.Tests
{
    // Telemetry state is process-global: every test touching it lives in this
    // collection, which xUnit never runs in parallel with itself.
    [CollectionDefinition("telemetry", DisableParallelization = true)]
    public class TelemetryCollection
    {
    }

    [Collection("telemetry")]
    public class TelemetryTests : IDisposable
    {
        public TelemetryTests()
        {
            TelemetrySender.AutoSend = false; // the tests call FlushAsync themselves
            CKeyTelemetry.ResetForTesting();
        }

        public void Dispose()
        {
            TelemetrySender.Endpoint = new Uri(CKeyTelemetry.DefaultEndpoint);
            CKeyTelemetry.ResetForTesting();
            CKey.Telemetry = CKey.TelemetryLocal; // nothing from the test run is ever sent
        }

        [Fact]
        public void DefaultsToOne()
        {
            Assert.True(CKeyTelemetry.IsAvailable);
            Assert.Equal(1, CKey.Telemetry);
        }

        [Theory]
        [InlineData(0)]
        [InlineData(1)]
        [InlineData(2)]
        public void AcceptsZeroOneTwo(int mode)
        {
            CKey.Telemetry = mode;
            Assert.Equal(mode, CKey.Telemetry);
        }

        [Theory]
        [InlineData(-1)]
        [InlineData(3)]
        [InlineData(int.MaxValue)]
        public void RejectsOtherValuesAndKeepsTheMode(int mode)
        {
            CKey.Telemetry = 2;
            Assert.Throws<ArgumentOutOfRangeException>(() => CKey.Telemetry = mode);
            Assert.Equal(2, CKey.Telemetry); // never silently reset to 1
        }

        [Fact]
        public void ScanSessionCountsASuccess()
        {
            CKey.Telemetry = 2;
            var symbol = CKeySymbol.Encode("session");
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            using (var session = new CKeyScanSession())
            {
                Assert.Equal("session", session.ProcessFrame(frame, w, h, stride)?.Text);
                Assert.True(session.HasDecoded);
                Assert.NotNull(session.ProcessFrame(frame, w, h, stride)); // later decodes still returned
            }
            CKeyTelemetryCounters c = CKeyTelemetry.Snapshot();
            Assert.Equal(1UL, c.SessionsStarted);
            Assert.Equal(1UL, c.SessionsSucceeded);
            Assert.Equal(0UL, c.SessionsFailed);
            Assert.Equal(2UL, c.Frames);
            Assert.Equal(1UL, c.Decoded[0]); // p0
            Assert.Equal(1UL, c.TimeToDecodeMs.Count);
            Assert.Equal(2UL, c.FrameMs.Count);
        }

        [Fact]
        public void PrintLayoutCountsAsP0_1()
        {
            CKey.Telemetry = 2;
            var symbol = CKeySymbol.Encode("print", layout: Layout.Print);
            byte[] frame = Frames.Frame(symbol, out int w, out int h, out int stride);
            using (var session = new CKeyScanSession()) Assert.NotNull(session.ProcessFrame(frame, w, h, stride));
            Assert.Equal(1UL, CKeyTelemetry.Snapshot().Decoded[1]);
        }

        [Fact]
        public void SessionWithoutDecodeFails()
        {
            CKey.Telemetry = 2;
            var blank = new byte[64 * 64];
            using (var session = new CKeyScanSession())
            {
                Assert.Null(session.ProcessFrame(blank, 64, 64, 64));
                session.Restart(); // ends the first session as failed
            }
            CKeyTelemetryCounters c = CKeyTelemetry.Snapshot();
            Assert.Equal(2UL, c.SessionsStarted);
            Assert.Equal(2UL, c.SessionsFailed);
            Assert.Equal(0UL, c.SessionsSucceeded);
        }

        [Fact]
        public void ModeZeroCountsNothingAndPurges()
        {
            CKey.Telemetry = 2;
            using (new CKeyScanSession())
            {
            }
            Assert.Equal(1UL, CKeyTelemetry.Snapshot().SessionsStarted);
            CKey.Telemetry = 0;
            using (new CKeyScanSession())
            {
            }
            Assert.Equal(0UL, CKeyTelemetry.Snapshot().SessionsStarted);
        }

        [Fact]
        public void ExportImportRoundTrip()
        {
            CKey.Telemetry = 2;
            using (new CKeyScanSession())
            {
            }
            string state = CKeyTelemetry.ExportState();
            Assert.StartsWith("{\"state\":1", state);
            CKeyTelemetry.ResetForTesting();
            CKey.Telemetry = 2;
            CKeyTelemetry.ImportState(state);
            Assert.Equal(1UL, CKeyTelemetry.Snapshot().SessionsStarted);
            Assert.Throws<CKeyException>(() => CKeyTelemetry.ImportState("{\"nope\":1}"));
        }

        [Fact]
        public async Task FlushSendsOneBatchToTheEndpoint()
        {
            // Build an unsent delta that is due immediately: deltas restored by
            // import are exempt from the minimum age, and a fresh process has no
            // hourly wait.
            CKey.Telemetry = 1;
            using (new CKeyScanSession())
            {
            }
            string state = CKeyTelemetry.ExportState();
            CKeyTelemetry.ResetForTesting();
            CKeyTelemetry.ImportState(state);

            int port = FreePort();
            using var listener = new HttpListener();
            listener.Prefixes.Add($"http://localhost:{port}/");
            listener.Start();
            TelemetrySender.Endpoint = new Uri($"http://localhost:{port}/api/telemetry/v1/batch");

            Task<string> server = Task.Run(async () =>
            {
                HttpListenerContext context = await listener.GetContextAsync();
                string body;
                using (var reader = new StreamReader(context.Request.InputStream, Encoding.UTF8)) body = await reader.ReadToEndAsync();
                string contentType = context.Request.ContentType ?? string.Empty;
                context.Response.StatusCode = 204;
                context.Response.Close();
                return contentType + "\n" + body;
            });

            bool accepted = await CKeyTelemetry.FlushAsync().WaitAsync(TimeSpan.FromSeconds(30));
            Assert.True(accepted);
            string[] request = (await server.WaitAsync(TimeSpan.FromSeconds(30))).Split('\n');
            Assert.StartsWith("application/json", request[0]);
            Assert.Contains("\"schema\":1", request[1]);
            Assert.Contains("\"sdk\":\"dotnet\"", request[1]);
            Assert.Contains($"\"sdk_version\":\"{CKey.Version}\"", request[1]);
            Assert.Contains("\"sessions\":{\"started\":1,\"succeeded\":0,\"failed\":1}", request[1]);

            // Accepted: nothing more is due (and the hourly cadence has started).
            Assert.False(await CKeyTelemetry.FlushAsync());
        }

        [Fact]
        public async Task NetworkFailureKeepsTheBatch()
        {
            CKey.Telemetry = 1;
            using (new CKeyScanSession())
            {
            }
            string state = CKeyTelemetry.ExportState();
            CKeyTelemetry.ResetForTesting();
            CKeyTelemetry.ImportState(state);
            TelemetrySender.Endpoint = new Uri($"http://127.0.0.1:{FreePort()}/"); // nothing listens
            Assert.False(await CKeyTelemetry.FlushAsync());
            // Kept: the delta still holds the session (it was merged back).
            Assert.Contains("\"unsent\":{\"sessions\":{\"started\":1", CKeyTelemetry.ExportState());
        }

        [Fact]
        public void VersionMatchesTheTelemetrySchema()
        {
            Assert.Matches(@"^0\.[0-9]+\.[0-9]+(-(build|dev)\.[0-9]{1,6})?$", CKey.Version);
            Assert.False(string.IsNullOrEmpty(CKey.CoreVersion));
        }

        private static int FreePort()
        {
            var l = new TcpListener(IPAddress.Loopback, 0);
            l.Start();
            int port = ((IPEndPoint)l.LocalEndpoint).Port;
            l.Stop();
            return port;
        }
    }
}
