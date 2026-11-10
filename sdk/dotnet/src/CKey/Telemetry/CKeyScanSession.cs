using System;
using System.Diagnostics;
using System.Threading;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>
    /// A continuous scanning session (for example while a camera view is
    /// visible). It decodes frames with <see cref="CKeyDecoder"/> and drives the
    /// telemetry session defined in docs/adoption-and-telemetry.md: it starts
    /// when the session is created, succeeds at the first decode, and fails when
    /// it is disposed (or restarted) without one, or after 30 s without a decode.
    /// Frames and their processing times are counted. Nothing about the payload
    /// is recorded. With telemetry off (mode 0) it is a plain decoder loop.
    /// </summary>
    /// <example>
    /// <code>
    /// using var session = new CKeyScanSession();
    /// foreach (var frame in camera.Frames)
    /// {
    ///     CKeyResult? result = session.ProcessFrame(frame.Gray, frame.Width, frame.Height, frame.Stride);
    ///     if (result != null) { Show(result.Text, result.Action); break; }
    /// }
    /// </code>
    /// </example>
    public sealed class CKeyScanSession : IDisposable
    {
        private int _decoded;
        private int _disposed;

        /// <summary>Starts a session.</summary>
        public CKeyScanSession()
        {
            Begin();
        }

        /// <summary>True once a frame of this session has decoded.</summary>
        public bool HasDecoded => Volatile.Read(ref _decoded) != 0;

        /// <summary>
        /// Decodes one grayscale frame and counts it. Returns the result, or null
        /// when nothing decodes. Later decodes in the same session are still
        /// returned but are not counted again.
        /// </summary>
        public CKeyResult? ProcessFrame(ReadOnlySpan<byte> gray, int width, int height, int stride)
        {
            if (Volatile.Read(ref _disposed) != 0) throw new ObjectDisposedException(nameof(CKeyScanSession));
            long started = Stopwatch.GetTimestamp();
            CKeyResult? result = CKeyDecoder.Decode(gray, width, height, stride);
            long elapsedTicks = Stopwatch.GetTimestamp() - started;
            RecordFrame(TimeSpan.FromSeconds(elapsedTicks / (double)Stopwatch.Frequency));
            if (result != null) RecordSuccess(result);
            return result;
        }

        /// <summary>
        /// Counts a frame processed by the host's own pipeline (when it does not
        /// call <see cref="ProcessFrame"/>).
        /// </summary>
        public void RecordFrame(TimeSpan processingTime)
        {
            if (!CKeyTelemetry.IsAvailable) return;
            double ms = Math.Round(Math.Max(0, processingTime.TotalMilliseconds));
            uint processing = ms >= uint.MaxValue ? uint.MaxValue : (uint)ms;
            NativeTelemetry.ckey_telemetry_frame(MonotonicClock.NowMs, processing);
        }

        /// <summary>Records the session's first valid decode (later calls are ignored).</summary>
        public void RecordSuccess(CKeyResult result)
        {
            if (result == null) throw new ArgumentNullException(nameof(result));
            if (Interlocked.Exchange(ref _decoded, 1) != 0) return;
            if (!CKeyTelemetry.IsAvailable) return;
            NativeTelemetry.ckey_telemetry_session_success(MonotonicClock.NowMs, result.TelemetryProfileCode);
        }

        /// <summary>
        /// "Scan again": ends the current session (failed if it had no decode)
        /// and starts a new one.
        /// </summary>
        public void Restart()
        {
            if (Volatile.Read(ref _disposed) != 0) throw new ObjectDisposedException(nameof(CKeyScanSession));
            Interlocked.Exchange(ref _decoded, 0);
            Begin(); // the core ends an open session as failed first
        }

        /// <summary>Ends the session (failed if it had no decode).</summary>
        public void Dispose()
        {
            if (Interlocked.Exchange(ref _disposed, 1) != 0) return;
            if (!CKeyTelemetry.IsAvailable) return;
            NativeTelemetry.ckey_telemetry_session_end(MonotonicClock.NowMs);
        }

        private static void Begin()
        {
            if (!CKeyTelemetry.IsAvailable) return;
            CKeyTelemetry.EnsureClient();
            NativeTelemetry.ckey_telemetry_session_begin(MonotonicClock.NowMs);
            if (NativeTelemetry.ckey_telemetry_get_mode() == CKey.TelemetryOn) TelemetrySender.EnsureStarted();
        }
    }
}
