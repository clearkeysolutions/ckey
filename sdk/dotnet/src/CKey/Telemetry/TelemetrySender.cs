using System;
using System.Globalization;
using System.Net.Http;
using System.Net.Http.Headers;
using System.Threading;
using System.Threading.Tasks;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>
    /// The HTTP half of telemetry (the core never does networking). A
    /// thread-pool timer asks the core whether a batch is due
    /// (<c>ckey_telemetry_should_send</c>: mode 1, something to send, at most
    /// once per hour); if so it takes the batch, POSTs it asynchronously and
    /// reports the HTTP status (0 for a network error) back to the core, which
    /// keeps or drops the batch per docs/adoption-and-telemetry.md. Nothing
    /// here runs on a scan thread or blocks a caller.
    /// </summary>
    internal static class TelemetrySender
    {
        private static readonly object Gate = new object();
        private static Timer? _timer;
        private static int _sending;
        private static HttpClient? _client;

        /// <summary>How often the timer asks the core; the core enforces the hourly cadence.</summary>
        private static readonly TimeSpan Poll = TimeSpan.FromMinutes(1);

        /// <summary>The batch endpoint; tests point it at a local listener.</summary>
        internal static Uri Endpoint { get; set; } = new Uri(CKeyTelemetry.DefaultEndpoint);

        /// <summary>TEST ONLY: false keeps the poll timer from starting, so tests drive sends themselves.</summary>
        internal static bool AutoSend { get; set; } = true;

        /// <summary>Starts the poll timer once. It does not keep the process alive.</summary>
        internal static void EnsureStarted()
        {
            if (_timer != null || !AutoSend) return;
            lock (Gate)
            {
                if (_timer != null) return;
                _timer = new Timer(state => { _ = TrySendAsync(); }, null, Poll, Poll);
            }
        }

        private static HttpClient Client
        {
            get
            {
                if (_client != null) return _client;
                lock (Gate)
                {
                    // No cookies, no credentials, no identifying headers.
                    var handler = new HttpClientHandler { UseCookies = false, UseDefaultCredentials = false, AllowAutoRedirect = false };
                    return _client ??= new HttpClient(handler) { Timeout = TimeSpan.FromSeconds(30) };
                }
            }
        }

        /// <summary>One attempt: true when a batch was posted and accepted (2xx).</summary>
        internal static async Task<bool> TrySendAsync()
        {
            if (Interlocked.Exchange(ref _sending, 1) == 1) return false;
            try
            {
                if (!CKeyTelemetry.IsAvailable) return false;
                CKeyTelemetry.EnsureClient();
                if (NativeTelemetry.ckey_telemetry_should_send(MonotonicClock.NowMs) == 0) return false;
                if (!TakeBatch(out byte[] body, out int length, out ulong batchId)) return false;

                int status = 0;
                try
                {
                    using var content = new ByteArrayContent(body, 0, length);
                    content.Headers.ContentType = new MediaTypeHeaderValue("application/json");
                    using HttpResponseMessage response = await Client.PostAsync(Endpoint, content).ConfigureAwait(false);
                    status = (int)response.StatusCode;
                }
                catch (Exception)
                {
                    status = 0; // network error, timeout, no connectivity: the core keeps the batch
                }
                if (status < 0 || status > 999) status = 0;
                // A mode switch while in flight makes the id stale; the core then ignores it.
                NativeTelemetry.ckey_telemetry_batch_result(batchId, status, MonotonicClock.NowMs);
                return status >= 200 && status <= 299;
            }
            catch (Exception)
            {
                return false; // telemetry never surfaces errors to the host
            }
            finally
            {
                Volatile.Write(ref _sending, 0);
            }
        }

        private static unsafe bool TakeBatch(out byte[] body, out int length, out ulong batchId)
        {
            byte[] day = Native.ToUtf8Z(DateTime.UtcNow.ToString("yyyy-MM-dd", CultureInfo.InvariantCulture))!;
            body = new byte[4096]; // CKEY_TELEMETRY_MAX_BODY_BYTES; the body is always well under it
            nuint len = 0;
            ulong id = 0;
            int status;
            fixed (byte* d = day)
            {
                fixed (byte* b = body) status = NativeTelemetry.ckey_telemetry_take_batch(d, b, (nuint)body.Length, &len, &id);
                if (status == (int)CKeyStatus.InsufficientBuffer)
                {
                    body = new byte[Native.ToInt32Length(len + 1, "Telemetry batch")];
                    fixed (byte* b = body) status = NativeTelemetry.ckey_telemetry_take_batch(d, b, (nuint)body.Length, &len, &id);
                }
            }
            length = (int)len;
            batchId = id;
            return status == 0 && id != 0 && length > 0;
        }
    }
}
