using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Threading.Tasks;
using OpenCKey.Interop;
using Xunit;

namespace OpenCKey.Tests
{
    // The core against tests/vectors/safe-actions-v1.json (an independent
    // implementation's envelopes), then the receiving app's policy end to end
    // over the native core with a fixed clock.
    public class SafeActionsTests
    {
        private const string Key = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
        private static readonly DateTimeOffset Now = DateTimeOffset.FromUnixTimeSeconds(1794000000);

        private static JsonElement Vectors(string file = "safe-actions-v1.json")
        {
            for (var dir = new DirectoryInfo(AppContext.BaseDirectory); dir != null; dir = dir.Parent)
            {
                string path = Path.Combine(dir.FullName, "tests", "vectors", file);
                if (File.Exists(path)) return JsonDocument.Parse(File.ReadAllText(path)).RootElement;
            }
            throw new FileNotFoundException("tests/vectors/" + file);
        }

        private static JsonElement Vector(string name) => Vectors().GetProperty("vectors").EnumerateArray().First(v => v.GetProperty("name").GetString() == name);
        private static string Payload(string name) => Vector(name).GetProperty("payload").GetString()!;

        [Fact]
        public void StructLayout()
        {
            Assert.Equal(1736, System.Runtime.InteropServices.Marshal.SizeOf<NativeSafeAction>());
            Assert.Equal(688, (int)System.Runtime.InteropServices.Marshal.OffsetOf<NativeSafeAction>(nameof(NativeSafeAction.IssuedAt)));
            Assert.Equal(1728, (int)System.Runtime.InteropServices.Marshal.OffsetOf<NativeSafeAction>(nameof(NativeSafeAction.ParamsLength)));
        }

        [Fact]
        public void EveryVectorVerdict()
        {
            int n = 0;
            var core = new CKeySafeActions("com.viadere").Core;
            foreach (var v in Vectors().GetProperty("vectors").EnumerateArray())
            {
                byte[] payload = Encoding.UTF8.GetBytes(v.GetProperty("payload").GetString()!);
                byte[] key = CKeySafeActions.HexKey(v.GetProperty("public_key").GetString())!;
                int status = core(payload, key, v.GetProperty("now").GetInt64(), v.GetProperty("max_lifetime").GetInt32(), out var fields, out string message);
                string expect = v.GetProperty("expect").GetString()!;
                bool ok = expect switch
                {
                    "ok" => status == 0,
                    "integrity" => status == 6,
                    "expired" => status == 7,
                    "invalid" => status == 1,
                    "invalid_or_integrity" => status == 1 || status == 6,
                    _ => false,
                };
                Assert.True(ok, $"{v.GetProperty("name").GetString()}: expected {expect}, got {status} {message}");
                if (expect == "ok")
                {
                    var f = v.GetProperty("fields");
                    Assert.Equal(f.GetProperty("issuer").GetString(), fields!.Issuer);
                    Assert.Equal(f.GetProperty("request_id").GetString(), fields.RequestId);
                    Assert.Equal(f.GetProperty("expires_at").GetInt64(), fields.ExpiresAt);
                    Assert.Equal(f.GetProperty("scope").GetInt32(), (int)fields.Scope);
                    Assert.Equal(f.GetProperty("params").GetString(), fields.ParamsText);
                }
                n++;
            }
            Assert.True(n >= 20);
        }

        // tests/vectors/safe-actions-es256-v1.json: ES256 (ECDSA P-256) issuer keys,
        // compressed and uncompressed. A key of the wrong length never reaches the core.
        [Fact]
        public void Es256VectorVerdicts()
        {
            int n = 0;
            var core = new CKeySafeActions("com.viadere").Core;
            foreach (var v in Vectors("safe-actions-es256-v1.json").GetProperty("vectors").EnumerateArray())
            {
                string name = v.GetProperty("name").GetString()!;
                string expect = v.GetProperty("expect").GetString()!;
                byte[]? key = CKeySafeActions.HexKey(v.GetProperty("public_key").GetString());
                if (key == null)
                {
                    Assert.Equal("invalid", expect);
                    Assert.Throws<ArgumentException>(() => new CKeySafeActions("com.viadere").Trust("viadere.com", "k", v.GetProperty("public_key").GetString()!));
                    n++;
                    continue;
                }
                byte[] payload = Encoding.UTF8.GetBytes(v.GetProperty("payload").GetString()!);
                int status = core(payload, key, v.GetProperty("now").GetInt64(), v.GetProperty("max_lifetime").GetInt32(), out var fields, out string message);
                bool ok = expect switch
                {
                    "ok" => status == 0,
                    "integrity" => status == 6,
                    "expired" => status == 7,
                    "invalid" => status == 1,
                    "invalid_or_integrity" => status == 1 || status == 6,
                    _ => false,
                };
                Assert.True(ok, $"{name}: expected {expect}, got {status} {message}");
                if (expect == "ok") Assert.Equal(v.GetProperty("fields").GetProperty("issuer").GetString(), fields!.Issuer);
                n++;
            }
            Assert.True(n >= 9);
        }

        private static CKeySafeActions Actions(Func<string, string?>? fetch = null)
        {
            var a = new CKeySafeActions("com.viadere");
            a.Clock = () => Now;
            a.Fetch = (url, _) => Task.FromResult(fetch?.Invoke(url));
            return a;
        }

        [Fact]
        public async Task SameDeviceRunsOnceWithoutPrompt()
        {
            var ran = new List<CKeyVerifiedSafeAction>();
            var a = Actions().Trust("citeory.com", "2026-11", Key).Register("open.library", new[] { "citeory.com" }, x => ran.Add(x));
            string text = Payload("valid same-device action");
            var outcome = await a.HandleAsync(text);
            Assert.Equal(CKeySafeActionOutcomeKind.Performed, outcome.Kind);
            Assert.Single(ran);
            Assert.Equal("{\"item\":\"apa-7\"}", ran[0].ParamsText);
            var replay = await a.HandleAsync(text);
            Assert.Equal(CKeySafeActionOutcomeKind.Rejected, replay.Kind);
            Assert.Equal("this code has already been used", replay.Reason);
            Assert.Single(ran);
        }

        [Fact]
        public async Task PolicyChecksRunNothing()
        {
            int ran = 0;
            string text = Payload("valid same-device action");
            Assert.Equal(CKeySafeActionOutcomeKind.NotASafeAction, (await Actions().HandleAsync("https://citeory.com")).Kind);
            Assert.Equal(CKeySafeActionOutcomeKind.Rejected, (await Actions().HandleAsync("ckey-action:1:AAAA")).Kind);
            var other = new CKeySafeActions("com.other") { Clock = () => Now };
            other.Trust("citeory.com", "2026-11", Key).Register("open.library", new[] { "citeory.com" }, _ => ran++);
            Assert.Contains("another app", (await other.HandleAsync(text)).Reason);
            Assert.Contains("does not accept", (await Actions().Trust("citeory.com", "2026-11", Key).HandleAsync(text)).Reason);
            Assert.Contains("may not request", (await Actions().Register("open.library", new[] { "example.org" }, _ => ran++).HandleAsync(text)).Reason);
            Assert.Contains("no trusted key", (await Actions().Register("open.library", new[] { "citeory.com" }, _ => ran++).HandleAsync(text)).Reason);
            var wrongKey = Actions().Trust("citeory.com", "2026-11", "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c")
                                   .Register("open.library", new[] { "citeory.com" }, _ => ran++);
            Assert.Equal(CKeySafeActionOutcomeKind.Rejected, (await wrongKey.HandleAsync(text)).Kind);
            var late = Actions().Trust("citeory.com", "2026-11", Key).Register("open.library", new[] { "citeory.com" }, _ => ran++);
            late.Clock = () => Now.AddHours(1);
            Assert.Equal(CKeySafeActionOutcomeKind.Rejected, (await late.HandleAsync(text)).Kind);
            Assert.Equal(0, ran);
        }

        [Fact]
        public async Task CrossDeviceNeedsPendingRequestInfoAndTheSdkPrompt()
        {
            int ran = 0;
            var urls = new List<string>();
            string status = "pending";
            var a = Actions(url =>
            {
                urls.Add(url);
                string id = url.Substring(url.LastIndexOf('/') + 1);
                return "{\"request_id\":\"" + id + "\",\"device\":\"Chrome on Windows\",\"location\":\"near Lagos, Nigeria\",\"requested_at\":\"2026-11-07T20:14:03Z\",\"status\":\"" + status + "\"}";
            })
                .Trust("citeory.com", "2026-11", Key, "https://citeory.com/api/ckey/requests/{request_id}")
                // Registered same-device; the envelope's cross-device scope wins.
                .Register("signin.request", new[] { "citeory.com" }, _ => ran++);
            var outcome = await a.HandleAsync(Payload("valid cross-device sign-in, no params"));
            Assert.Equal(CKeySafeActionOutcomeKind.NeedsConfirmation, outcome.Kind);
            Assert.Equal(new[] { "https://citeory.com/api/ckey/requests/sess-41fd0a9be2c74" }, urls);
            Assert.Equal(0, ran);
            var request = outcome.Request!;
            Assert.Contains("citeory.com (verified)", request.Message);
            Assert.Contains("near Lagos, Nigeria", request.Message);
            Assert.True(await request.Decide(true));
            Assert.Equal(1, ran);
            Assert.False(await request.Decide(true));
            Assert.Equal(1, ran);
        }

        [Fact]
        public async Task CrossDeviceWithoutPendingRequestIsRejected()
        {
            int ran = 0;
            var a = Actions(_ => "{\"request_id\":\"sess-41fd0a9be2c74\",\"status\":\"approved\"}")
                .Trust("citeory.com", "2026-11", Key, "https://citeory.com/r/{request_id}")
                .Register("signin.request", new[] { "citeory.com" }, _ => ran++);
            Assert.Equal(CKeySafeActionOutcomeKind.Rejected, (await a.HandleAsync(Payload("valid cross-device sign-in, no params"))).Kind);
            var b = Actions(_ => null)
                .Trust("citeory.com", "2026-11", Key, "https://citeory.com/r/{request_id}")
                .Register("open.library", new[] { "citeory.com" }, CKeySafeActionScope.CrossDevice, null, _ => { ran++; return Task.CompletedTask; });
            Assert.Equal(CKeySafeActionOutcomeKind.Rejected, (await b.HandleAsync(Payload("valid same-device action"))).Kind);
            Assert.Equal(0, ran);
        }

        [Fact]
        public async Task DeclineRunsNothing()
        {
            int ran = 0;
            var a = Actions(_ => "{\"request_id\":\"sess-41fd0a9be2c74\",\"status\":\"pending\"}")
                .Trust("citeory.com", "2026-11", Key, "https://login.citeory.com/r/{request_id}")
                .Register("signin.request", new[] { "citeory.com" }, _ => ran++);
            var request = (await a.HandleAsync(Payload("valid cross-device sign-in, no params"))).Request!;
            Assert.False(await request.Decide(false));
            Assert.False(await request.Decide(true));
            Assert.True(request.IsDecided);
            Assert.Equal(0, ran);
        }

        [Fact]
        public async Task KeyFiles()
        {
            var fetched = new List<string>();
            var a = new CKeySafeActions("com.viadere", useKeyFiles: true) { Clock = () => Now };
            a.Fetch = (url, _) =>
            {
                fetched.Add(url);
                return Task.FromResult<string?>("{\"issuer\":\"citeory.com\",\"keys\":[{\"id\":\"2026-11\",\"alg\":\"Ed25519\",\"public_key\":\"" + Key + "\",\"not_after\":\"2027-11-01T00:00:00Z\"}]}");
            };
            int ran = 0;
            a.Register("open.library", new[] { "citeory.com" }, _ => ran++);
            Assert.Equal(CKeySafeActionOutcomeKind.Performed, (await a.HandleAsync(Payload("valid same-device action"))).Kind);
            Assert.Equal(new[] { "https://citeory.com/.well-known/ckey-safe-actions.json" }, fetched);
            Assert.Equal(1, ran);
        }

        [Fact]
        public void RequestInfoUrlStaysOnTheIssuersDomain()
        {
            Assert.Equal("https://citeory.com/r/sess-1", CKeySafeActions.RequestInfoUrl("https://citeory.com/r/{request_id}", "citeory.com", "sess-1"));
            Assert.Equal("https://api.citeory.com/r/x", CKeySafeActions.RequestInfoUrl("https://api.citeory.com/r/{request_id}", "citeory.com", "x"));
            Assert.Null(CKeySafeActions.RequestInfoUrl("http://citeory.com/r/{request_id}", "citeory.com", "x"));
            Assert.Null(CKeySafeActions.RequestInfoUrl("https://evilciteory.com/r/{request_id}", "citeory.com", "x"));
            Assert.Null(CKeySafeActions.RequestInfoUrl("https://citeory.com.evil.example/r/{request_id}", "citeory.com", "x"));
            Assert.Null(CKeySafeActions.RequestInfoUrl("https://citeory.com@evil.example/r/{request_id}", "citeory.com", "x"));
            Assert.Null(CKeySafeActions.RequestInfoUrl(null, "citeory.com", "x"));
        }

        [Fact]
        public void ArgumentChecks()
        {
            Assert.Throws<ArgumentException>(() => new CKeySafeActions("not an app id"));
            Assert.Throws<ArgumentOutOfRangeException>(() => new CKeySafeActions("com.viadere", 3601));
            Assert.Throws<ArgumentException>(() => new CKeySafeActions("com.viadere").Trust("Citeory.com", "k", Key));
            Assert.Throws<ArgumentException>(() => new CKeySafeActions("com.viadere").Trust("citeory.com", "k", "abc"));
            Assert.Throws<ArgumentException>(() => new CKeySafeActions("com.viadere").Register("Open", new[] { "citeory.com" }, _ => { }));
            Assert.Throws<ArgumentException>(() => new CKeySafeActions("com.viadere").Register("open", Array.Empty<string>(), _ => { }));
        }

        [Fact]
        public void MiniJsonReader()
        {
            var m = (Dictionary<string, object?>)MiniJson.Parse("{\"a\":[1,true,null,\"x\\u0041\\n\"],\"b\":{}}")!;
            Assert.Equal(new object?[] { 1.0, true, null, "xA\n" }, (List<object?>)m["a"]!);
            Assert.Null(MiniJson.Parse("{\"a\":1} trailing"));
            Assert.Null(MiniJson.Parse(new string('[', 40) + new string(']', 40)));
            Assert.Null(MiniJson.Parse("{\"a\":"));
        }
    }
}
