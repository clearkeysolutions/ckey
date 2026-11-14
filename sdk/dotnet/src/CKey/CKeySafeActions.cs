using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Net.Http;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using OpenCKey.Interop;

// CKey Safe Actions for the receiving app (spec/CKey-Safe-Actions.md).
//
// The SDK, not the app, decides whether a Safe Action runs:
//   - anything that is not a verified Safe Action for this app runs nothing;
//   - a verified same-device action runs its registered handler at once;
//   - a verified cross-device action runs only after the SDK's own confirmation
//     (CKey.Wpf's CKeySafeActionPrompt, or CKeySafeActionConsolePrompt), which
//     names the verified issuer and the requesting device and place.
// CKeySafeActionRequest.Decide is internal: an app has no API that approves a
// cross-device action without that confirmation. Verification is the C core's.
namespace OpenCKey
{
    /// <summary>Where a Safe Action's effect happens.</summary>
    public enum CKeySafeActionScope
    {
        /// <summary>The result stays on the scanning device, inside this app. Runs at once.</summary>
        SameDevice = 0,
        /// <summary>The action approves something elsewhere (another browser, a payment). Needs the SDK's confirmation.</summary>
        CrossDevice = 1,
    }

    /// <summary>A Safe Action whose signature, app, action, lifetime and freshness were verified.</summary>
    public sealed class CKeyVerifiedSafeAction
    {
        internal CKeyVerifiedSafeAction(string issuer, string app, string action, string keyId, string requestId,
            CKeySafeActionScope scope, long issuedAt, long expiresAt, byte[] parameters)
        {
            Issuer = issuer; App = app; Action = action; KeyId = keyId; RequestId = requestId;
            Scope = scope; IssuedAt = issuedAt; ExpiresAt = expiresAt; Params = parameters;
        }

        /// <summary>The issuer's DNS name.</summary>
        public string Issuer { get; }
        /// <summary>The receiving app's ID.</summary>
        public string App { get; }
        /// <summary>The registered action name.</summary>
        public string Action { get; }
        /// <summary>The issuer key that signed it.</summary>
        public string KeyId { get; }
        /// <summary>The single-use request ID.</summary>
        public string RequestId { get; }
        /// <summary>The scope the issuer declared.</summary>
        public CKeySafeActionScope Scope { get; }
        /// <summary>Unix seconds.</summary>
        public long IssuedAt { get; }
        /// <summary>Unix seconds.</summary>
        public long ExpiresAt { get; }
        /// <summary>The parameters as signed (UTF-8 JSON is recommended).</summary>
        public IReadOnlyList<byte> Params { get; }

        /// <summary><see cref="Params"/> as UTF-8 text, or null when they are not valid UTF-8.</summary>
        public string? ParamsText
        {
            get
            {
                try { return new UTF8Encoding(false, true).GetString(Params.ToArray()); }
                catch (ArgumentException) { return null; }
            }
        }
    }

    /// <summary>What the issuer says about a pending cross-device request: who is asking.</summary>
    public sealed class CKeySafeActionRequestInfo
    {
        internal CKeySafeActionRequestInfo(string? device, string? location, string? requestedAt)
        {
            Device = device; Location = location; RequestedAt = requestedAt;
        }

        /// <summary>For example "Chrome on Windows".</summary>
        public string? Device { get; }
        /// <summary>For example "near Lagos, Nigeria".</summary>
        public string? Location { get; }
        /// <summary>When the request was made, as the issuer reports it.</summary>
        public string? RequestedAt { get; }
    }

    /// <summary>
    /// A verified cross-device Safe Action waiting for the person to confirm it.
    /// Show it with CKey.Wpf's <c>CKeySafeActionPrompt</c> or <see cref="CKeySafeActionConsolePrompt"/>;
    /// approval is internal to the SDK.
    /// </summary>
    public sealed class CKeySafeActionRequest
    {
        private readonly Func<CKeyVerifiedSafeAction, Task> _handler;
        private int _decided;

        internal CKeySafeActionRequest(string title, CKeyVerifiedSafeAction details, CKeySafeActionRequestInfo info, Func<CKeyVerifiedSafeAction, Task> handler)
        {
            Title = title; Details = details; RequestInfo = info; _handler = handler;
        }

        /// <summary>The confirmation's title.</summary>
        public string Title { get; }
        /// <summary>The verified action.</summary>
        public CKeyVerifiedSafeAction Details { get; }
        /// <summary>The issuer's account of who is asking.</summary>
        public CKeySafeActionRequestInfo RequestInfo { get; }
        /// <summary>True once the person approved or declined; a request is decided once.</summary>
        public bool IsDecided => Volatile.Read(ref _decided) != 0;

        /// <summary>The confirmation text every SDK prompt shows.</summary>
        public string Message
        {
            get
            {
                var b = new StringBuilder();
                b.Append("Requested by ").Append(Details.Issuer).Append(" (verified)\n");
                if (RequestInfo.Device != null) b.Append("Device: ").Append(RequestInfo.Device).Append('\n');
                if (RequestInfo.Location != null) b.Append("Location: ").Append(RequestInfo.Location).Append('\n');
                if (RequestInfo.RequestedAt != null) b.Append("Requested: ").Append(RequestInfo.RequestedAt).Append('\n');
                b.Append('\n').Append("Only continue if this is the device and place you are using right now. ")
                 .Append("Someone may be trying to use a code meant for their own session.");
                return b.ToString();
            }
        }

        /// <summary>Called only by the SDK's prompts. Returns true when the handler ran.</summary>
        internal async Task<bool> Decide(bool approved)
        {
            if (Interlocked.Exchange(ref _decided, 1) != 0) return false;
            if (!approved) return false;
            await _handler(Details).ConfigureAwait(false);
            return true;
        }
    }

    /// <summary>The kind of <see cref="CKeySafeActionOutcome"/>.</summary>
    public enum CKeySafeActionOutcomeKind
    {
        /// <summary>Not a Safe Action payload: follow the ordinary payload rules (<see cref="CKeyPayload"/>).</summary>
        NotASafeAction,
        /// <summary>Claims to be a Safe Action but could not be verified. Run nothing; say so, with <see cref="CKeySafeActionOutcome.Reason"/>.</summary>
        Rejected,
        /// <summary>A verified same-device action whose handler has run.</summary>
        Performed,
        /// <summary>A verified cross-device action: show <see cref="CKeySafeActionOutcome.Request"/> in an SDK prompt.</summary>
        NeedsConfirmation,
    }

    /// <summary>The result of <see cref="CKeySafeActions.HandleAsync"/>.</summary>
    public sealed class CKeySafeActionOutcome
    {
        private CKeySafeActionOutcome(CKeySafeActionOutcomeKind kind, string? reason = null, CKeyVerifiedSafeAction? action = null, CKeySafeActionRequest? request = null)
        {
            Kind = kind; Reason = reason; Action = action; Request = request;
        }

        internal static readonly CKeySafeActionOutcome NotASafeAction = new CKeySafeActionOutcome(CKeySafeActionOutcomeKind.NotASafeAction);
        internal static CKeySafeActionOutcome Rejected(string reason) => new CKeySafeActionOutcome(CKeySafeActionOutcomeKind.Rejected, reason);
        internal static CKeySafeActionOutcome Performed(CKeyVerifiedSafeAction a) => new CKeySafeActionOutcome(CKeySafeActionOutcomeKind.Performed, action: a);
        internal static CKeySafeActionOutcome Confirm(CKeySafeActionRequest r) => new CKeySafeActionOutcome(CKeySafeActionOutcomeKind.NeedsConfirmation, action: r.Details, request: r);

        /// <summary>What happened.</summary>
        public CKeySafeActionOutcomeKind Kind { get; }
        /// <summary>Why it was rejected (Rejected only).</summary>
        public string? Reason { get; }
        /// <summary>The verified action (Performed and NeedsConfirmation).</summary>
        public CKeyVerifiedSafeAction? Action { get; }
        /// <summary>The request to confirm (NeedsConfirmation only).</summary>
        public CKeySafeActionRequest? Request { get; }
    }

    /// <summary>
    /// The receiving app's Safe Actions policy. Thread-safe.
    /// </summary>
    /// <example>
    /// <code>
    /// var actions = new CKeySafeActions("com.viadere")
    ///     .Trust("citeory.com", "2026-11", "&lt;64 hex digits&gt;", "https://citeory.com/api/ckey/requests/{request_id}")
    ///     .Register("signin.request", new[] { "citeory.com" }, CKeySafeActionScope.CrossDevice, null, a => SignInAsync(a));
    /// var outcome = await actions.HandleAsync(text);
    /// if (outcome.Kind == CKeySafeActionOutcomeKind.NeedsConfirmation) await CKeySafeActionPrompt.ShowAsync(window, outcome.Request!);
    /// </code>
    /// </example>
    public sealed class CKeySafeActions
    {
        /// <summary>The Safe Action text prefix.</summary>
        public const string Prefix = "ckey-action:1:";
        private const int MaxLifetimeCap = 3600;
        private static readonly TimeSpan KeyFileTtl = TimeSpan.FromHours(1);
        private const long ClockToleranceMs = 120_000;
        private static readonly Regex AppIdPattern = new Regex("^[A-Za-z0-9._-]{1,255}$", RegexOptions.CultureInvariant);
        private static readonly Regex IssuerPattern = new Regex("^[a-z0-9.-]+\\.[a-z0-9-]+$", RegexOptions.CultureInvariant);
        private static readonly Regex ActionPattern = new Regex("^[a-z0-9._-]{1,64}$", RegexOptions.CultureInvariant);
        private static readonly Regex KeyIdPattern = new Regex("^[A-Za-z0-9._-]{1,32}$", RegexOptions.CultureInvariant);
        private static readonly Regex HexKeyPattern = new Regex("^(?:[0-9A-Fa-f]{64}|0[23][0-9A-Fa-f]{64}|04[0-9A-Fa-f]{128})$", RegexOptions.CultureInvariant);

        /// <summary>The core's verdict. Replaced only by unit tests.</summary>
        internal delegate int CoreFn(byte[] payload, byte[]? publicKey, long nowSeconds, int maxLifetime, out CKeyVerifiedSafeAction? fields, out string message);

        private sealed class Trusted { public byte[] Key = Array.Empty<byte>(); public string? RequestInfo; }
        private sealed class Registered { public HashSet<string> Issuers = new HashSet<string>(); public CKeySafeActionScope Scope; public string? Title; public Func<CKeyVerifiedSafeAction, Task> Handler = _ => Task.CompletedTask; }
        private sealed class KeyFile { public DateTimeOffset At; public Dictionary<string, (byte[] Key, DateTimeOffset? NotAfter)> Keys = new Dictionary<string, (byte[], DateTimeOffset?)>(); public string? RequestInfo; }

        private readonly object _lock = new object();
        private readonly Dictionary<string, Trusted> _keys = new Dictionary<string, Trusted>();
        private readonly Dictionary<string, Registered> _actions = new Dictionary<string, Registered>();
        private readonly Dictionary<string, long> _seen = new Dictionary<string, long>();
        private readonly Dictionary<string, KeyFile?> _keyFiles = new Dictionary<string, KeyFile?>();
        private readonly HttpClient _http;

        internal CoreFn Core = NativeCore;
        internal Func<string, CancellationToken, Task<string?>> Fetch;
        internal Func<DateTimeOffset> Clock = () => DateTimeOffset.UtcNow;

        /// <param name="appId">This app's ID, for example <c>com.viadere</c>.</param>
        /// <param name="maxLifetime">The longest lifetime this app accepts, 1 to 3600 seconds.</param>
        /// <param name="useKeyFiles">Also look up keys in <c>https://&lt;issuer&gt;/.well-known/ckey-safe-actions.json</c>. Pinned keys are preferred for sign-in and payments.</param>
        /// <param name="http">The HttpClient for key files and request information; one is created when null.</param>
        public CKeySafeActions(string appId, int maxLifetime = 600, bool useKeyFiles = false, HttpClient? http = null)
        {
            if (appId == null || !AppIdPattern.IsMatch(appId)) throw new ArgumentException("appId must be an app ID such as com.example.app", nameof(appId));
            if (maxLifetime < 1 || maxLifetime > MaxLifetimeCap) throw new ArgumentOutOfRangeException(nameof(maxLifetime), "maxLifetime is 1 to 3600 seconds");
            AppId = appId;
            MaxLifetime = maxLifetime;
            UseKeyFiles = useKeyFiles;
            _http = http ?? new HttpClient(new HttpClientHandler { AllowAutoRedirect = false }) { Timeout = TimeSpan.FromSeconds(5) };
            Fetch = HttpsGet;
        }

        /// <summary>This app's ID.</summary>
        public string AppId { get; }
        /// <summary>The longest lifetime accepted, in seconds.</summary>
        public int MaxLifetime { get; }
        /// <summary>Whether issuer key files are consulted.</summary>
        public bool UseKeyFiles { get; }

        /// <summary>True when text is a Safe Action payload. Says nothing about whether it is valid.</summary>
        public static bool IsSafeAction(string? text) => text != null && text.StartsWith(Prefix, StringComparison.Ordinal) && text.Length > Prefix.Length;

        /// <summary>Pins an issuer's public key: Ed25519 (64 hex digits) or ES256 (P-256, SEC 1: 66 hex digits compressed or 130 uncompressed); the length says which. <paramref name="requestInfo"/> is the issuer's request_info URL template, required for cross-device actions.</summary>
        public CKeySafeActions Trust(string issuer, string keyId, string publicKeyHex, string? requestInfo = null)
        {
            if (issuer == null || !IssuerPattern.IsMatch(issuer)) throw new ArgumentException("issuer is a lower-case domain name", nameof(issuer));
            if (keyId == null || !KeyIdPattern.IsMatch(keyId)) throw new ArgumentException("keyId is 1 to 32 letters, digits, . _ or -", nameof(keyId));
            byte[] key = HexKey(publicKeyHex) ?? throw new ArgumentException("a public key is 64 (Ed25519), 66 or 130 (ES256) hexadecimal digits", nameof(publicKeyHex));
            lock (_lock) _keys[issuer + " " + keyId] = new Trusted { Key = key, RequestInfo = requestInfo };
            return this;
        }

        /// <summary>
        /// Registers an action this app accepts from <paramref name="issuers"/>. The stricter of
        /// <paramref name="scope"/> and the envelope's own scope applies.
        /// </summary>
        public CKeySafeActions Register(string action, IEnumerable<string> issuers, CKeySafeActionScope scope, string? title, Func<CKeyVerifiedSafeAction, Task> handler)
        {
            if (action == null || !ActionPattern.IsMatch(action)) throw new ArgumentException("action is 1 to 64 of a-z 0-9 . _ -", nameof(action));
            var set = new HashSet<string>(issuers ?? throw new ArgumentNullException(nameof(issuers)), StringComparer.Ordinal);
            if (set.Count == 0) throw new ArgumentException("name the issuer or issuers allowed to request this action", nameof(issuers));
            if (handler == null) throw new ArgumentNullException(nameof(handler));
            lock (_lock) _actions[action] = new Registered { Issuers = set, Scope = scope, Title = ShortText(title, 80), Handler = handler };
            return this;
        }

        /// <summary>Registers a same-device action with a synchronous handler.</summary>
        public CKeySafeActions Register(string action, IEnumerable<string> issuers, Action<CKeyVerifiedSafeAction> handler)
        {
            if (handler == null) throw new ArgumentNullException(nameof(handler));
            return Register(action, issuers, CKeySafeActionScope.SameDevice, null, a => { handler(a); return Task.CompletedTask; });
        }

        /// <summary>Handles a decoded text payload.</summary>
        public async Task<CKeySafeActionOutcome> HandleAsync(string text, CancellationToken cancellationToken = default)
        {
            if (!IsSafeAction(text)) return CKeySafeActionOutcome.NotASafeAction;
            byte[] bytes = Encoding.UTF8.GetBytes(text);
            if (Core(bytes, null, 0, MaxLifetimeCap, out var claimed, out string parseMessage) != 0 || claimed == null)
                return CKeySafeActionOutcome.Rejected(string.IsNullOrEmpty(parseMessage) ? "malformed Safe Action" : parseMessage);
            if (claimed.App != AppId) return CKeySafeActionOutcome.Rejected($"this code is for another app ({claimed.App})");
            Registered? registered;
            lock (_lock) _actions.TryGetValue(claimed.Action, out registered);
            if (registered == null) return CKeySafeActionOutcome.Rejected($"this app does not accept the action {claimed.Action}");
            if (!registered.Issuers.Contains(claimed.Issuer)) return CKeySafeActionOutcome.Rejected($"{claimed.Issuer} may not request {claimed.Action}");
            Trusted? trusted = await KeyFor(claimed.Issuer, claimed.KeyId, cancellationToken).ConfigureAwait(false);
            if (trusted == null) return CKeySafeActionOutcome.Rejected($"no trusted key {claimed.KeyId} for {claimed.Issuer}");
            long nowMs = Clock().ToUnixTimeMilliseconds();
            int status = Core(bytes, trusted.Key, nowMs / 1000, MaxLifetime, out var verified, out string message);
            if (status != 0 || verified == null) return CKeySafeActionOutcome.Rejected(string.IsNullOrEmpty(message) ? $"verification failed ({(CKeyStatus)status})" : message);
            if (verified.App != claimed.App || verified.Action != claimed.Action || verified.Issuer != claimed.Issuer) return CKeySafeActionOutcome.Rejected("verification mismatch");
            lock (_lock)
            {
                foreach (var stale in _seen.Where(kv => kv.Value < nowMs).Select(kv => kv.Key).ToList()) _seen.Remove(stale);
                if (_seen.ContainsKey(verified.RequestId)) return CKeySafeActionOutcome.Rejected("this code has already been used");
                _seen[verified.RequestId] = verified.ExpiresAt * 1000 + ClockToleranceMs;
            }
            bool crossDevice = verified.Scope == CKeySafeActionScope.CrossDevice || registered.Scope == CKeySafeActionScope.CrossDevice;
            if (!crossDevice)
            {
                await registered.Handler(verified).ConfigureAwait(false);
                return CKeySafeActionOutcome.Performed(verified);
            }
            var info = await RequestInfo(trusted.RequestInfo, verified.Issuer, verified.RequestId, cancellationToken).ConfigureAwait(false);
            if (info == null) return CKeySafeActionOutcome.Rejected("the issuer did not confirm a pending request, so this cross-device action cannot run");
            return CKeySafeActionOutcome.Confirm(new CKeySafeActionRequest(registered.Title ?? $"Continue with {verified.Issuer}?", verified, info, registered.Handler));
        }

        private async Task<Trusted?> KeyFor(string issuer, string keyId, CancellationToken ct)
        {
            lock (_lock) if (_keys.TryGetValue(issuer + " " + keyId, out var pinned)) return pinned;
            if (!UseKeyFiles) return null;
            KeyFile? file = await LoadKeyFile(issuer, ct).ConfigureAwait(false);
            if (file == null || !file.Keys.TryGetValue(keyId, out var entry)) return null;
            if (entry.NotAfter.HasValue && entry.NotAfter.Value < Clock()) return null;
            return new Trusted { Key = entry.Key, RequestInfo = file.RequestInfo };
        }

        private async Task<KeyFile?> LoadKeyFile(string issuer, CancellationToken ct)
        {
            DateTimeOffset now = Clock();
            lock (_lock)
                if (_keyFiles.TryGetValue(issuer, out var cached) && (cached == null ? false : now - cached.At < KeyFileTtl)) return cached;
            string? body = await Fetch("https://" + issuer + "/.well-known/ckey-safe-actions.json", ct).ConfigureAwait(false);
            KeyFile? file = null;
            if (body != null && MiniJson.Parse(body) is Dictionary<string, object?> root && root.TryGetValue("issuer", out var i) && (i as string) == issuer)
            {
                file = new KeyFile { At = now, RequestInfo = root.TryGetValue("request_info", out var ri) ? ri as string : null };
                if (root.TryGetValue("keys", out var keys) && keys is List<object?> list)
                {
                    foreach (var k in list)
                    {
                        if (!(k is Dictionary<string, object?> m)) continue;
                        if (!(m.TryGetValue("id", out var id) && id is string idText && KeyIdPattern.IsMatch(idText))) continue;
                        string? algName = m.TryGetValue("alg", out var alg) ? alg as string : null;
                        if (algName != "Ed25519" && algName != "ES256") continue;
                        byte[]? key = m.TryGetValue("public_key", out var pk) && pk is string hex ? HexKey(hex) : null;
                        // The key's length must agree with its alg (32 bytes Ed25519; 33 or 65 ES256).
                        if (key == null || (key.Length == 32) != (algName == "Ed25519")) continue;
                        DateTimeOffset? notAfter = null;
                        if (m.TryGetValue("not_after", out var na) && na is string naText)
                        {
                            if (!DateTimeOffset.TryParse(naText, CultureInfo.InvariantCulture, DateTimeStyles.AssumeUniversal, out var parsed)) continue;
                            notAfter = parsed;
                        }
                        file.Keys[idText] = (key, notAfter);
                    }
                }
            }
            // A failed fetch is not cached, so a transient failure does not lock an issuer out for an hour.
            if (file != null) lock (_lock) _keyFiles[issuer] = file;
            return file;
        }

        private async Task<CKeySafeActionRequestInfo?> RequestInfo(string? template, string issuer, string requestId, CancellationToken ct)
        {
            string? url = RequestInfoUrl(template, issuer, requestId);
            if (url == null) return null;
            string? body = await Fetch(url, ct).ConfigureAwait(false);
            if (body == null || !(MiniJson.Parse(body) is Dictionary<string, object?> info)) return null;
            string? Field(string name) => info.TryGetValue(name, out var v) ? v as string : null;
            if (Field("request_id") != requestId || Field("status") != "pending") return null;
            return new CKeySafeActionRequestInfo(ShortText(Field("device")), ShortText(Field("location")), ShortText(Field("requested_at"), 40));
        }

        /// <summary>Request information must come over HTTPS from the issuer's own domain.</summary>
        internal static string? RequestInfoUrl(string? template, string issuer, string requestId)
        {
            if (template == null) return null;
            string text = template.Replace("{request_id}", Uri.EscapeDataString(requestId));
            if (!Uri.TryCreate(text, UriKind.Absolute, out var uri)) return null;
            string host = uri.Host.ToLowerInvariant();
            if (uri.Scheme != "https" || uri.UserInfo.Length != 0 || !(host == issuer || host.EndsWith("." + issuer, StringComparison.Ordinal))) return null;
            return uri.AbsoluteUri;
        }

        internal static byte[]? HexKey(string? hex)
        {
            if (hex == null || !HexKeyPattern.IsMatch(hex)) return null;
            var key = new byte[hex.Length / 2];
            for (int i = 0; i < key.Length; i++) key[i] = byte.Parse(hex.Substring(i * 2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            return key;
        }

        private static string? ShortText(string? v, int max = 120) => v != null && v.Length > 0 && v.Length <= max ? v : null;

        private async Task<string?> HttpsGet(string url, CancellationToken ct)
        {
            if (!url.StartsWith("https://", StringComparison.Ordinal)) return null;
            try
            {
                using var request = new HttpRequestMessage(HttpMethod.Get, url);
                request.Headers.Accept.ParseAdd("application/json");
                using var response = await _http.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, ct).ConfigureAwait(false);
                if ((int)response.StatusCode != 200) return null;
                if (response.Content.Headers.ContentLength > 64 * 1024) return null;
                byte[] body = await response.Content.ReadAsByteArrayAsync().ConfigureAwait(false);
                return body.Length > 64 * 1024 ? null : Encoding.UTF8.GetString(body);
            }
            catch (Exception e) when (e is HttpRequestException || e is TaskCanceledException || e is InvalidOperationException)
            {
                return null;
            }
        }

        private static unsafe int NativeCore(byte[] payload, byte[]? publicKey, long nowSeconds, int maxLifetime, out CKeyVerifiedSafeAction? fields, out string message)
        {
            NativeSafeAction sa = default;
            NativeDiagnostics diag = default;
            diag.AbiVersion = NativeConstants.AbiVersion;
            int status;
            fixed (byte* p = payload)
            {
                if (publicKey == null)
                {
                    status = NativeMethods.ckey_safe_action_parse(p, (nuint)payload.Length, &sa, &diag);
                }
                else
                {
                    // 32 bytes: Ed25519. 33 or 65 bytes: ES256 (ECDSA P-256, SEC 1).
                    if (publicKey.Length != 32 && publicKey.Length != 33 && publicKey.Length != 65)
                        throw new ArgumentException("a public key is 32 (Ed25519), 33 or 65 (ES256) bytes", nameof(publicKey));
                    fixed (byte* k = publicKey)
                        status = publicKey.Length == 32
                            ? NativeMethods.ckey_safe_action_verify(p, (nuint)payload.Length, k, (ulong)Math.Max(0, nowSeconds), (uint)maxLifetime, &sa, &diag)
                            : NativeMethods.ckey_safe_action_verify_key(p, (nuint)payload.Length, 2 /* CKEY_SAFE_ACTION_ES256 */, k, (nuint)publicKey.Length, (ulong)Math.Max(0, nowSeconds), (uint)maxLifetime, &sa, &diag);
                }
            }
            if (status != 0)
            {
                fields = null;
                message = Native.Message(ref diag);
                return status;
            }
            int n = (int)Math.Min((ulong)sa.ParamsLength, 1024UL);
            var parameters = new byte[n];
            for (int i = 0; i < n; i++) parameters[i] = sa.Params[i];
            fields = new CKeyVerifiedSafeAction(
                Native.FromUtf8Z(sa.Issuer, 254) ?? "", Native.FromUtf8Z(sa.App, 256) ?? "", Native.FromUtf8Z(sa.Action, 65) ?? "",
                Native.FromUtf8Z(sa.KeyId, 33) ?? "", Native.FromUtf8Z(sa.RequestId, 65) ?? "",
                sa.Scope == 1 ? CKeySafeActionScope.CrossDevice : CKeySafeActionScope.SameDevice,
                (long)Math.Min(sa.IssuedAt, long.MaxValue), (long)Math.Min(sa.ExpiresAt, long.MaxValue), parameters);
            message = string.Empty;
            return 0;
        }
    }

    /// <summary>
    /// The SDK's console confirmation for a cross-device Safe Action. It reads the
    /// answer from the interactive console only; with redirected input it declines.
    /// </summary>
    public static class CKeySafeActionConsolePrompt
    {
        /// <summary>Asks on the console. Returns true when the person approved and the handler ran.</summary>
        public static Task<bool> AskAsync(CKeySafeActionRequest request)
        {
            if (request == null) throw new ArgumentNullException(nameof(request));
            if (request.IsDecided) return Task.FromResult(false);
            if (Console.IsInputRedirected) return request.Decide(false);
            Console.Out.WriteLine(request.Title);
            Console.Out.WriteLine(request.Message);
            Console.Out.Write("Type 'continue' to approve, anything else to cancel: ");
            string? answer = Console.ReadLine();
            return request.Decide(string.Equals(answer?.Trim(), "continue", StringComparison.OrdinalIgnoreCase));
        }
    }

    /// <summary>A small JSON reader for key files and request information.</summary>
    internal static class MiniJson
    {
        internal static object? Parse(string text)
        {
            try
            {
                int i = 0;
                object? v = Value(text, ref i, 0);
                Ws(text, ref i);
                return i == text.Length ? v : null;
            }
            catch (Exception e) when (e is FormatException || e is IndexOutOfRangeException || e is ArgumentOutOfRangeException || e is OverflowException)
            {
                return null;
            }
        }

        private static void Ws(string s, ref int i) { while (i < s.Length && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++; }
        private static void Expect(bool ok) { if (!ok) throw new FormatException(); }

        private static object? Value(string s, ref int i, int depth)
        {
            Expect(depth < 32);
            Ws(s, ref i);
            char c = s[i];
            if (c == '{')
            {
                i++;
                var m = new Dictionary<string, object?>(StringComparer.Ordinal);
                Ws(s, ref i);
                if (s[i] == '}') { i++; return m; }
                while (true)
                {
                    Ws(s, ref i); string k = Str(s, ref i); Ws(s, ref i); Expect(s[i] == ':'); i++;
                    m[k] = Value(s, ref i, depth + 1); Ws(s, ref i);
                    if (s[i] == ',') { i++; continue; }
                    Expect(s[i] == '}'); i++; return m;
                }
            }
            if (c == '[')
            {
                i++;
                var a = new List<object?>();
                Ws(s, ref i);
                if (s[i] == ']') { i++; return a; }
                while (true)
                {
                    a.Add(Value(s, ref i, depth + 1)); Ws(s, ref i);
                    if (s[i] == ',') { i++; continue; }
                    Expect(s[i] == ']'); i++; return a;
                }
            }
            if (c == '"') return Str(s, ref i);
            if (string.CompareOrdinal(s, i, "true", 0, 4) == 0) { i += 4; return true; }
            if (string.CompareOrdinal(s, i, "false", 0, 5) == 0) { i += 5; return false; }
            if (string.CompareOrdinal(s, i, "null", 0, 4) == 0) { i += 4; return null; }
            int start = i;
            while (i < s.Length && (char.IsDigit(s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
            Expect(i > start);
            return double.Parse(s.Substring(start, i - start), NumberStyles.Float, CultureInfo.InvariantCulture);
        }

        private static string Str(string s, ref int i)
        {
            Expect(s[i] == '"'); i++;
            var b = new StringBuilder();
            while (true)
            {
                char c = s[i++];
                if (c == '"') return b.ToString();
                if (c == '\\')
                {
                    char e = s[i++];
                    switch (e)
                    {
                        case 'n': b.Append('\n'); break;
                        case 't': b.Append('\t'); break;
                        case 'r': b.Append('\r'); break;
                        case 'b': b.Append('\b'); break;
                        case 'f': b.Append('\f'); break;
                        case 'u': b.Append((char)int.Parse(s.Substring(i, 4), NumberStyles.HexNumber, CultureInfo.InvariantCulture)); i += 4; break;
                        default: b.Append(e); break;
                    }
                }
                else
                {
                    Expect(c >= ' ');
                    b.Append(c);
                }
            }
        }
    }
}
