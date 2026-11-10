using System;
using System.Diagnostics;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Threading.Tasks;
using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>
    /// Library-wide settings. Opting out of telemetry is one line:
    /// <code>CKey.Telemetry = 0;</code>
    /// </summary>
    public static class CKey
    {
        /// <summary>0: no counting, nothing sent.</summary>
        public const int TelemetryOff = 0;
        /// <summary>1: local counting and anonymous aggregate batches (the default).</summary>
        public const int TelemetryOn = 1;
        /// <summary>2: local counting, never sent.</summary>
        public const int TelemetryLocal = 2;

        /// <summary>
        /// Telemetry mode: 0 off, 1 on (default), 2 local only. Any other value
        /// throws <see cref="ArgumentOutOfRangeException"/> and leaves the mode
        /// unchanged. See docs/adoption-and-telemetry.md.
        /// </summary>
        public static int Telemetry
        {
            get => CKeyTelemetry.Mode;
            set => CKeyTelemetry.Mode = value;
        }

        /// <summary>The SDK (NuGet package) version, e.g. "0.1.0-build.63".</summary>
        public static string Version => CKeyTelemetry.SdkVersion;

        /// <summary>The native core's software version (<c>ckey_get_software_version</c>).</summary>
        public static unsafe string CoreVersion
        {
            get
            {
                Native.EnsureAbi();
                return Native.FromUtf8Z(NativeMethods.ckey_get_software_version()) ?? string.Empty;
            }
        }

        /// <summary>The native core's ABI version.</summary>
        public static int AbiVersion
        {
            get
            {
                Native.EnsureAbi();
                return (int)NativeMethods.ckey_get_abi_version();
            }
        }
    }

    /// <summary>Aggregate histogram (count, sum, ten buckets).</summary>
    public sealed class CKeyTelemetryHistogram
    {
        internal CKeyTelemetryHistogram(ulong count, ulong sum, ulong[] buckets)
        {
            Count = count;
            Sum = sum;
            Buckets = buckets;
        }

        /// <summary>Observations.</summary>
        public ulong Count { get; }
        /// <summary>Sum of observations (ms).</summary>
        public ulong Sum { get; }
        /// <summary>Ten buckets with inclusive upper edges (see docs/adoption-and-telemetry.md).</summary>
        public ulong[] Buckets { get; }
    }

    /// <summary>Local telemetry totals (<c>ckey_telemetry_counters</c>).</summary>
    public sealed class CKeyTelemetryCounters
    {
        // By value: the copy is a local, so its address can be taken directly.
        internal unsafe CKeyTelemetryCounters(NativeTelemetryCounters c)
        {
            SessionsStarted = c.SessionsStarted;
            SessionsSucceeded = c.SessionsSucceeded;
            SessionsFailed = c.SessionsFailed;
            Frames = c.Frames;
            NativeTelemetryCounters* p = &c;
            Decoded = new ulong[NativeConstants.TelemetryProfiles];
            for (int i = 0; i < Decoded.Length; i++) Decoded[i] = p->Decoded[i];
            TimeToDecodeMs = Histogram(&p->TimeToDecodeMs);
            FrameMs = Histogram(&p->FrameMs);
        }

        private static unsafe CKeyTelemetryHistogram Histogram(NativeTelemetryHistogram* h)
        {
            var buckets = new ulong[NativeConstants.TelemetryBuckets];
            for (int i = 0; i < buckets.Length; i++) buckets[i] = h->Buckets[i];
            return new CKeyTelemetryHistogram(h->Count, h->Sum, buckets);
        }

        /// <summary>Scan sessions started.</summary>
        public ulong SessionsStarted { get; }
        /// <summary>Sessions that decoded a symbol.</summary>
        public ulong SessionsSucceeded { get; }
        /// <summary>Sessions that ended or timed out without a decode.</summary>
        public ulong SessionsFailed { get; }
        /// <summary>Frames processed.</summary>
        public ulong Frames { get; }
        /// <summary>Decodes by profile: p0, p0_1 (print), p1, p2, p3.</summary>
        public ulong[] Decoded { get; }
        /// <summary>Time from session start to first decode.</summary>
        public CKeyTelemetryHistogram TimeToDecodeMs { get; }
        /// <summary>Per-frame processing time.</summary>
        public CKeyTelemetryHistogram FrameMs { get; }
    }

    /// <summary>
    /// Anonymous, aggregate scan telemetry (specification §9,
    /// docs/adoption-and-telemetry.md). Counts sessions, frames, decodes per
    /// profile and coarse durations; never payloads, URLs, identifiers or
    /// location. Recording happens only through <see cref="CKeyScanSession"/>;
    /// <see cref="CKeyDecoder"/> and rendering record nothing. In mode 1 a
    /// background timer posts at most one batch per hour, off the scan thread.
    /// </summary>
    public static class CKeyTelemetry
    {
        private static readonly object Gate = new object();
        private static int _available = -1; // -1 unknown, 0 no, 1 yes
        private static bool _clientSet;

        /// <summary>The endpoint batches are posted to (CKEY_TELEMETRY_ENDPOINT).</summary>
        public const string DefaultEndpoint = "https://openckey.org/api/telemetry/v1/batch";

        /// <summary>
        /// True when the native library includes the telemetry module. A build
        /// with CKEY_DOTNET_TELEMETRY=OFF omits it; then the mode reads 0 and
        /// nothing is counted or sent.
        /// </summary>
        public static bool IsAvailable
        {
            get
            {
                if (_available < 0)
                {
                    lock (Gate)
                    {
                        if (_available < 0)
                        {
                            Native.EnsureAbi();
                            try
                            {
                                NativeTelemetry.ckey_telemetry_get_mode();
                                _available = 1;
                            }
                            catch (EntryPointNotFoundException)
                            {
                                _available = 0;
                            }
                        }
                    }
                }
                return _available == 1;
            }
        }

        /// <summary>Same as <see cref="CKey.Telemetry"/>.</summary>
        public static int Mode
        {
            get => IsAvailable ? NativeTelemetry.ckey_telemetry_get_mode() : CKey.TelemetryOff;
            set
            {
                if (value < CKey.TelemetryOff || value > CKey.TelemetryLocal)
                    throw new ArgumentOutOfRangeException(nameof(value), value,
                        "CKey.Telemetry must be 0 (off), 1 (on) or 2 (local only).");
                if (!IsAvailable)
                {
                    if (value == CKey.TelemetryOff) return;
                    throw new InvalidOperationException("This native ckey library was built without the telemetry module.");
                }
                EnsureClient();
                int status = NativeTelemetry.ckey_telemetry_set_mode(value);
                if (status != 0)
                    throw new ArgumentOutOfRangeException(nameof(value), value, $"ckey_telemetry_set_mode refused {value}.");
            }
        }

        /// <summary>Local totals since counting started (all zero in mode 0).</summary>
        public static unsafe CKeyTelemetryCounters Snapshot()
        {
            NativeTelemetryCounters c = default;
            if (IsAvailable) Native.Check(NativeTelemetry.ckey_telemetry_snapshot(&c), "ckey_telemetry_snapshot");
            return new CKeyTelemetryCounters(c);
        }

        /// <summary>
        /// Optional persistence: the local totals and unsent delta as compact
        /// JSON. Store it and pass it to <see cref="ImportState"/> at the next start.
        /// </summary>
        public static unsafe string ExportState()
        {
            if (!IsAvailable) throw new InvalidOperationException("This native ckey library was built without the telemetry module.");
            nuint length = 0;
            int status = NativeTelemetry.ckey_telemetry_export_state(null, 0, &length);
            if (status != (int)CKeyStatus.InsufficientBuffer) Native.Check(status, "ckey_telemetry_export_state");
            var buffer = new byte[Native.ToInt32Length(length + 1, "Telemetry state")];
            fixed (byte* b = buffer)
            {
                Native.Check(NativeTelemetry.ckey_telemetry_export_state(b, (nuint)buffer.Length, &length), "ckey_telemetry_export_state");
                return Native.FromUtf8Z(b, buffer.Length) ?? string.Empty;
            }
        }

        /// <summary>
        /// Adds a state saved by <see cref="ExportState"/> to the current counts.
        /// Call once at startup. Throws <see cref="CKeyException"/> for malformed
        /// state or in mode 0. Never changes the mode or re-enables sending.
        /// </summary>
        public static unsafe void ImportState(string state)
        {
            if (state == null) throw new ArgumentNullException(nameof(state));
            if (!IsAvailable) throw new InvalidOperationException("This native ckey library was built without the telemetry module.");
            byte[] json = Native.ToUtf8Z(state)!;
            fixed (byte* j = json) Native.Check(NativeTelemetry.ckey_telemetry_import_state(j), "ckey_telemetry_import_state");
            if (Mode == CKey.TelemetryOn) TelemetrySender.EnsureStarted();
        }

        /// <summary>
        /// Tries a send now, for example when the app moves to the background
        /// or is about to exit. Runs on the thread pool and never blocks the
        /// caller; the hourly cadence still applies, so this usually does
        /// nothing. Completes with true when a batch was accepted.
        /// </summary>
        public static Task<bool> FlushAsync() => Task.Run(() => TelemetrySender.TrySendAsync());

        internal static string SdkVersion
        {
            get
            {
                foreach (var attribute in typeof(CKeyTelemetry).Assembly.GetCustomAttributes<AssemblyMetadataAttribute>())
                {
                    if (attribute.Key == "CKeyVersion" && !string.IsNullOrEmpty(attribute.Value)) return attribute.Value!;
                }
                return "0.0.0";
            }
        }

        internal static string Platform
        {
            get
            {
#if NET5_0_OR_GREATER
                if (OperatingSystem.IsAndroid()) return "android";
                if (OperatingSystem.IsIOS() || OperatingSystem.IsMacCatalyst()) return "ios";
                if (OperatingSystem.IsBrowser()) return "web";
#endif
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Windows)) return "windows";
                if (RuntimeInformation.IsOSPlatform(OSPlatform.OSX)) return "macos";
                if (RuntimeInformation.IsOSPlatform(OSPlatform.Linux)) return "linux";
                return "other";
            }
        }

        /// <summary>Reports sdk "dotnet", this package's version and the platform (once).</summary>
        internal static void EnsureClient()
        {
            if (_clientSet || !IsAvailable) return;
            lock (Gate)
            {
                if (_clientSet) return;
                ApplyClient();
                _clientSet = true;
            }
        }

        private static unsafe void ApplyClient()
        {
            byte[] sdk = Native.ToUtf8Z("dotnet")!;
            byte[] platform = Native.ToUtf8Z(Platform)!;
            byte[] version = Native.ToUtf8Z(SdkVersion)!;
            fixed (byte* s = sdk)
            fixed (byte* p = platform)
            fixed (byte* v = version)
            {
                if (NativeTelemetry.ckey_telemetry_set_client(s, v, p) == 0) return;
                // A version outside the schema pattern: report the core's version instead.
                byte* core = NativeMethods.ckey_get_software_version();
                if (NativeTelemetry.ckey_telemetry_set_client(s, core, p) == 0) return;
                Debug.WriteLine("CKey: ckey_telemetry_set_client refused the client identity.");
            }
        }

        /// <summary>TEST ONLY: restores the core's telemetry defaults, then re-applies the client identity.</summary>
        internal static void ResetForTesting()
        {
            if (!IsAvailable) return;
            lock (Gate)
            {
                NativeTelemetry.ckey_telemetry_reset_for_testing();
                ApplyClient();
                _clientSet = true;
            }
        }
    }

    /// <summary>A monotonic millisecond clock shared by every telemetry call.</summary>
    internal static class MonotonicClock
    {
        private static readonly Stopwatch Watch = Stopwatch.StartNew();

        internal static ulong NowMs => (ulong)Watch.ElapsedMilliseconds;
    }
}
