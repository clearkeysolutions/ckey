package org.openckey.ckey

import java.net.HttpURLConnection
import java.net.URI
import java.net.URLEncoder

/**
 * CKey Safe Actions for the receiving app (spec/CKey-Safe-Actions.md).
 *
 * The SDK, not the app, decides whether a Safe Action runs:
 *  - anything that is not a verified Safe Action for this app runs nothing;
 *  - a verified same-device action runs its registered handler at once;
 *  - a verified cross-device action runs only after the SDK's own confirmation
 *    ([CKeySafeActionPrompt]), which names the verified issuer and the
 *    requesting device and place.
 *
 * There is no public way to approve a [CKeySafeActionRequest] other than that
 * prompt. Verification (Ed25519, envelope, lifetime) is the C core's.
 *
 * ```
 * val actions = CKeySafeActions("com.viadere")
 *     .trust("citeory.com", "2026-11", "<64 hex digits>", requestInfo = "https://citeory.com/api/ckey/requests/{request_id}")
 *     .register("signin.request", listOf("citeory.com"), CKeySafeActionScope.CROSS_DEVICE) { action -> signIn(action) }
 * // Off the main thread (cross-device actions fetch the issuer's request information):
 * when (val outcome = actions.handle(text)) {
 *     is CKeySafeActionOutcome.NeedsConfirmation -> runOnUiThread { CKeySafeActionPrompt.show(activity, outcome.request) }
 *     is CKeySafeActionOutcome.Rejected -> showRejected(outcome.reason)
 *     else -> Unit
 * }
 * ```
 */
enum class CKeySafeActionScope {
    /** The result stays on the scanning device, inside this app. Runs at once. */
    SAME_DEVICE,
    /** The action approves something elsewhere (another browser, a payment). Needs the SDK's confirmation. */
    CROSS_DEVICE,
}

/** The fields of a Safe Action whose signature, app, action, lifetime and freshness were verified. */
data class CKeyVerifiedSafeAction(
    val issuer: String,
    val app: String,
    val action: String,
    val keyId: String,
    val requestId: String,
    val scope: CKeySafeActionScope,
    /** Unix seconds. */
    val issuedAt: Long,
    /** Unix seconds. */
    val expiresAt: Long,
    val params: ByteArray,
) {
    /** [params] as UTF-8 text, or null when they are not valid UTF-8. */
    val paramsText: String? get() = CKeyPayload.text(params) ?: if (params.isEmpty()) "" else null

    override fun equals(other: Any?): Boolean = other is CKeyVerifiedSafeAction && issuer == other.issuer &&
        app == other.app && action == other.action && keyId == other.keyId && requestId == other.requestId &&
        scope == other.scope && issuedAt == other.issuedAt && expiresAt == other.expiresAt && params.contentEquals(other.params)

    override fun hashCode(): Int = requestId.hashCode() * 31 + action.hashCode()
}

/** What the issuer says about a pending cross-device request: who is asking. */
data class CKeySafeActionRequestInfo(val device: String?, val location: String?, val requestedAt: String?)

/**
 * A verified cross-device Safe Action waiting for the person to confirm it.
 *
 * Show it with [CKeySafeActionPrompt.show]. Approval is internal to the SDK,
 * so an app cannot skip, replace or pre-approve the confirmation.
 */
class CKeySafeActionRequest internal constructor(
    val title: String,
    val details: CKeyVerifiedSafeAction,
    val requestInfo: CKeySafeActionRequestInfo,
    private val handler: (CKeyVerifiedSafeAction) -> Unit,
) {
    private var decided = false

    /** True once the person has approved or declined; a request is decided once. */
    val isDecided: Boolean get() = synchronized(this) { decided }

    /** Called only by [CKeySafeActionPrompt]. Returns true when the handler ran. */
    internal fun decide(approved: Boolean): Boolean {
        synchronized(this) {
            if (decided) return false
            decided = true
        }
        if (!approved) return false
        handler(details)
        return true
    }
}

/** The result of [CKeySafeActions.handle]. */
sealed class CKeySafeActionOutcome {
    /** Not a Safe Action payload: follow the ordinary payload rules ([CKeyPayload.classify]). */
    object NotASafeAction : CKeySafeActionOutcome()

    /**
     * Claims to be a Safe Action but could not be verified. Run nothing. Say
     * that the code claims to be a Safe Action but could not be verified, and why.
     */
    data class Rejected(val reason: String) : CKeySafeActionOutcome()

    /** A verified same-device action whose handler has run. */
    data class Performed(val action: CKeyVerifiedSafeAction) : CKeySafeActionOutcome()

    /** A verified cross-device action: show [request] with [CKeySafeActionPrompt.show]. */
    data class NeedsConfirmation(val request: CKeySafeActionRequest) : CKeySafeActionOutcome()
}

/**
 * The receiving app's Safe Actions policy. Thread-safe. [handle] blocks while a
 * cross-device action fetches the issuer's request information (and while an
 * issuer key file is fetched, when [useKeyFiles] is on), so call it off the
 * main thread.
 *
 * @param appId this app's ID, for example `com.viadere`
 * @param maxLifetime the longest lifetime this app accepts, 1 to 3600 seconds (default 600)
 * @param useKeyFiles also look up keys in `https://<issuer>/.well-known/ckey-safe-actions.json`;
 *        pinned keys ([trust]) are preferred for sign-in and payments
 */
class CKeySafeActions @JvmOverloads constructor(
    val appId: String,
    val maxLifetime: Int = 600,
    val useKeyFiles: Boolean = false,
) {
    /** The core's verdict: (status, fields-or-diagnostic). Replaced only by unit tests. */
    internal fun interface Core {
        fun run(payload: ByteArray, publicKey: ByteArray?, nowSeconds: Long, maxLifetime: Int): Pair<Int, String>
    }

    /** HTTPS GET returning the body of a 200 response, or null. Replaced only by unit tests. */
    internal fun interface Fetcher {
        fun get(url: String): String?
    }

    internal var core: Core = Core { payload, key, now, max ->
        val status = IntArray(1)
        val text = CKeyNative.nativeSafeAction(payload, key, now, max, status)
        status[0] to text
    }
    internal var fetcher: Fetcher = Fetcher(::httpsGet)
    internal var clock: () -> Long = System::currentTimeMillis

    private class Trusted(val key: ByteArray, val requestInfo: String?)
    private class Registered(val issuers: Set<String>, val scope: CKeySafeActionScope, val title: String?, val handler: (CKeyVerifiedSafeAction) -> Unit)
    private class KeyFile(val at: Long, val keys: Map<String, Pair<ByteArray, Long?>>, val requestInfo: String?)

    private val keys = HashMap<String, Trusted>()
    private val actions = HashMap<String, Registered>()
    private val seen = HashMap<String, Long>()
    private val keyFiles = HashMap<String, KeyFile?>()
    private val keyFileTimes = HashMap<String, Long>()

    init {
        require(APP_ID.matches(appId)) { "appId must be an app ID such as com.example.app" }
        require(maxLifetime in 1..MAX_LIFETIME_CAP) { "maxLifetime is 1 to 3600 seconds" }
    }

    /**
     * Pins an issuer's Ed25519 public key (64 hexadecimal digits). [requestInfo]
     * is the issuer's `request_info` URL template, required for cross-device actions.
     */
    @JvmOverloads
    fun trust(issuer: String, keyId: String, publicKeyHex: String, requestInfo: String? = null): CKeySafeActions {
        require(ISSUER.matches(issuer)) { "issuer is a lower-case domain name" }
        require(KEY_ID.matches(keyId)) { "keyId is 1 to 32 letters, digits, . _ or -" }
        val key = hexKey(publicKeyHex) ?: throw IllegalArgumentException("a public key is 64 hexadecimal digits")
        synchronized(this) { keys["$issuer $keyId"] = Trusted(key, requestInfo) }
        return this
    }

    /**
     * Registers an action this app accepts from [issuers]. The stricter of
     * [scope] and the envelope's own scope applies: a cross-device action never
     * runs without the SDK's confirmation.
     */
    @JvmOverloads
    fun register(
        action: String,
        issuers: List<String>,
        scope: CKeySafeActionScope = CKeySafeActionScope.SAME_DEVICE,
        title: String? = null,
        handler: (CKeyVerifiedSafeAction) -> Unit,
    ): CKeySafeActions {
        require(ACTION.matches(action)) { "action is 1 to 64 of a-z 0-9 . _ -" }
        require(issuers.isNotEmpty()) { "name the issuer or issuers allowed to request this action" }
        synchronized(this) { actions[action] = Registered(issuers.toSet(), scope, shortText(title, 80), handler) }
        return this
    }

    /** Handles a decoded text payload. Blocking; call off the main thread. */
    fun handle(text: String): CKeySafeActionOutcome {
        if (!isSafeAction(text)) return CKeySafeActionOutcome.NotASafeAction
        val bytes = text.toByteArray(Charsets.UTF_8)
        val (parseStatus, parsed) = core.run(bytes, null, 0, MAX_LIFETIME_CAP)
        if (parseStatus != 0) return CKeySafeActionOutcome.Rejected(parsed.ifEmpty { "malformed Safe Action" })
        val claimed = fields(parsed) ?: return CKeySafeActionOutcome.Rejected("malformed Safe Action")
        if (claimed.app != appId) return reject("this code is for another app (${claimed.app})")
        val registered = synchronized(this) { actions[claimed.action] } ?: return reject("this app does not accept the action ${claimed.action}")
        if (claimed.issuer !in registered.issuers) return reject("${claimed.issuer} may not request ${claimed.action}")
        val trusted = keyFor(claimed.issuer, claimed.keyId) ?: return reject("no trusted key ${claimed.keyId} for ${claimed.issuer}")
        val nowMs = clock()
        val (status, result) = core.run(bytes, trusted.key, nowMs / 1000, maxLifetime)
        if (status != 0) return reject(result.ifEmpty { "verification failed (status $status)" })
        val verified = fields(result) ?: return reject("malformed Safe Action")
        // The core verified exactly what was parsed; anything else is a core defect, and runs nothing.
        if (verified.app != claimed.app || verified.action != claimed.action || verified.issuer != claimed.issuer) return reject("verification mismatch")
        synchronized(this) {
            seen.entries.removeIf { it.value < nowMs }
            if (seen.containsKey(verified.requestId)) return reject("this code has already been used")
            seen[verified.requestId] = verified.expiresAt * 1000 + CLOCK_TOLERANCE_MS
        }
        val crossDevice = verified.scope == CKeySafeActionScope.CROSS_DEVICE || registered.scope == CKeySafeActionScope.CROSS_DEVICE
        if (!crossDevice) {
            registered.handler(verified)
            return CKeySafeActionOutcome.Performed(verified)
        }
        val info = requestInfo(trusted.requestInfo, verified.issuer, verified.requestId)
            ?: return reject("the issuer did not confirm a pending request, so this cross-device action cannot run")
        val title = registered.title ?: "Continue with ${verified.issuer}?"
        return CKeySafeActionOutcome.NeedsConfirmation(CKeySafeActionRequest(title, verified, info, registered.handler))
    }

    private fun reject(reason: String) = CKeySafeActionOutcome.Rejected(reason)

    private fun keyFor(issuer: String, keyId: String): Trusted? {
        synchronized(this) { keys["$issuer $keyId"] }?.let { return it }
        if (!useKeyFiles) return null
        val file = keyFile(issuer) ?: return null
        val (key, notAfter) = file.keys[keyId] ?: return null
        if (notAfter != null && notAfter < clock()) return null
        return Trusted(key, file.requestInfo)
    }

    private fun keyFile(issuer: String): KeyFile? {
        val now = clock()
        synchronized(this) {
            val at = keyFileTimes[issuer]
            if (at != null && now - at < KEY_FILE_TTL_MS) return keyFiles[issuer]
        }
        val parsed = fetcher.get("https://$issuer/.well-known/ckey-safe-actions.json")?.let { MiniJson.parse(it) } as? Map<*, *>
        var file: KeyFile? = null
        if (parsed != null && parsed["issuer"] == issuer) {
            val entries = HashMap<String, Pair<ByteArray, Long?>>()
            for (k in parsed["keys"] as? List<*> ?: emptyList<Any>()) {
                val m = k as? Map<*, *> ?: continue
                val id = m["id"] as? String ?: continue
                if (m["alg"] != "Ed25519" || !KEY_ID.matches(id)) continue
                val hex = m["public_key"] as? String ?: continue
                val key = hexKey(hex) ?: continue
                val notAfterText = m["not_after"] as? String
                val notAfter = if (notAfterText == null) null else (utcMillis(notAfterText) ?: continue)
                entries[id] = key to notAfter
            }
            file = KeyFile(now, entries, parsed["request_info"] as? String)
        }
        synchronized(this) { keyFiles[issuer] = file; keyFileTimes[issuer] = now }
        return file
    }

    private fun requestInfo(template: String?, issuer: String, requestId: String): CKeySafeActionRequestInfo? {
        val url = requestInfoUrl(template, issuer, requestId) ?: return null
        val info = fetcher.get(url)?.let { MiniJson.parse(it) } as? Map<*, *> ?: return null
        if (info["request_id"] != requestId || info["status"] != "pending") return null
        return CKeySafeActionRequestInfo(
            shortText(info["device"] as? String), shortText(info["location"] as? String), shortText(info["requested_at"] as? String, 40),
        )
    }

    companion object {
        const val PREFIX = "ckey-action:1:"
        private const val MAX_LIFETIME_CAP = 3600
        private const val KEY_FILE_TTL_MS = 3_600_000L
        private const val CLOCK_TOLERANCE_MS = 120_000L
        private val APP_ID = Regex("^[A-Za-z0-9._-]{1,255}$")
        private val ISSUER = Regex("^[a-z0-9.-]+\\.[a-z0-9-]+$")
        private val ACTION = Regex("^[a-z0-9._-]{1,64}$")
        private val KEY_ID = Regex("^[A-Za-z0-9._-]{1,32}$")

        /** True when [text] is a Safe Action payload. Says nothing about whether it is valid. */
        @JvmStatic fun isSafeAction(text: String): Boolean = text.startsWith(PREFIX) && text.length > PREFIX.length

        internal fun hexKey(hex: String): ByteArray? {
            if (!Regex("^[0-9A-Fa-f]{64}$").matches(hex)) return null
            return ByteArray(32) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() }
        }

        /** `YYYY-MM-DDTHH:MM:SSZ` (java.time needs API 26; this SDK supports 24). */
        internal fun utcMillis(text: String): Long? {
            if (!Regex("^\\d{4}-\\d{2}-\\d{2}T\\d{2}:\\d{2}:\\d{2}(\\.\\d+)?Z$").matches(text)) return null
            val f = java.text.SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss", java.util.Locale.ROOT)
            f.timeZone = java.util.TimeZone.getTimeZone("UTC")
            f.isLenient = false
            return runCatching { f.parse(text.substring(0, 19))?.time }.getOrNull()
        }

        private fun shortText(v: String?, max: Int = 120): String? = v?.takeIf { it.isNotEmpty() && it.length <= max }

        /** Request information must come over HTTPS from the issuer's own domain. */
        internal fun requestInfoUrl(template: String?, issuer: String, requestId: String): String? {
            if (template == null) return null
            val text = template.replace("{request_id}", URLEncoder.encode(requestId, "UTF-8"))
            val uri = runCatching { URI(text) }.getOrNull() ?: return null
            val host = uri.host?.lowercase() ?: return null
            if (uri.scheme != "https" || uri.userInfo != null || !(host == issuer || host.endsWith(".$issuer"))) return null
            return uri.toString()
        }

        /** Parses the bridge's line format; see nativeSafeAction in ckey_jni.cpp. */
        internal fun fields(text: String): CKeyVerifiedSafeAction? {
            val f = text.split('\n')
            if (f.size != 9) return null
            val scope = when (f[0]) { "0" -> CKeySafeActionScope.SAME_DEVICE; "1" -> CKeySafeActionScope.CROSS_DEVICE; else -> return null }
            val hex = f[8]
            if (hex.length % 2 != 0) return null
            val params = runCatching { ByteArray(hex.length / 2) { hex.substring(it * 2, it * 2 + 2).toInt(16).toByte() } }.getOrNull() ?: return null
            return CKeyVerifiedSafeAction(
                issuer = f[1], app = f[2], action = f[3], keyId = f[4], requestId = f[5], scope = scope,
                issuedAt = f[6].toLongOrNull() ?: return null, expiresAt = f[7].toLongOrNull() ?: return null, params = params,
            )
        }

        private fun httpsGet(url: String): String? = runCatching {
            val c = URI(url).toURL().openConnection() as HttpURLConnection
            try {
                if (c.url.protocol != "https") return null
                c.instanceFollowRedirects = false
                c.connectTimeout = 5000
                c.readTimeout = 5000
                c.setRequestProperty("Accept", "application/json")
                if (c.responseCode != 200) return null
                val out = java.io.ByteArrayOutputStream()
                c.inputStream.use { s ->
                    val buf = ByteArray(8192)
                    while (true) {
                        val n = s.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        if (out.size() > 64 * 1024) return null
                    }
                }
                String(out.toByteArray(), Charsets.UTF_8)
            } finally {
                c.disconnect()
            }
        }.getOrNull()
    }
}

/** A small JSON reader for key files and request information (objects, arrays, strings, numbers, literals). */
internal object MiniJson {
    fun parse(text: String): Any? = runCatching {
        val r = R(text)
        val v = r.value(0)
        r.ws()
        if (r.i != text.length) null else v
    }.getOrNull()

    private class R(val s: String) {
        var i = 0
        fun ws() { while (i < s.length && s[i] in " \t\r\n") i++ }
        fun value(depth: Int): Any? {
            require(depth < 32)
            ws()
            return when (s[i]) {
                '{' -> obj(depth)
                '[' -> arr(depth)
                '"' -> str()
                't' -> lit("true", true)
                'f' -> lit("false", false)
                'n' -> lit("null", null)
                else -> {
                    val start = i
                    while (i < s.length && (s[i].isDigit() || s[i] in "+-.eE")) i++
                    s.substring(start, i).toDouble()
                }
            }
        }
        fun obj(depth: Int): Map<String, Any?> {
            i++
            val m = LinkedHashMap<String, Any?>()
            ws()
            if (s[i] == '}') { i++; return m }
            while (true) {
                ws(); val k = str(); ws(); require(s[i] == ':'); i++
                m[k] = value(depth + 1); ws()
                if (s[i] == ',') { i++; continue }
                require(s[i] == '}'); i++
                return m
            }
        }
        fun arr(depth: Int): List<Any?> {
            i++
            val a = ArrayList<Any?>()
            ws()
            if (s[i] == ']') { i++; return a }
            while (true) {
                a.add(value(depth + 1)); ws()
                if (s[i] == ',') { i++; continue }
                require(s[i] == ']'); i++
                return a
            }
        }
        fun lit(word: String, v: Any?): Any? { require(s.startsWith(word, i)); i += word.length; return v }
        fun str(): String {
            require(s[i] == '"'); i++
            val b = StringBuilder()
            while (true) {
                val c = s[i++]
                when {
                    c == '"' -> return b.toString()
                    c == '\\' -> {
                        when (val e = s[i++]) {
                            'n' -> b.append('\n'); 't' -> b.append('\t'); 'r' -> b.append('\r')
                            'b' -> b.append('\b'); 'f' -> b.append('\u000c')
                            'u' -> { b.append(s.substring(i, i + 4).toInt(16).toChar()); i += 4 }
                            else -> b.append(e)
                        }
                    }
                    c < ' ' -> throw IllegalArgumentException("control character in string")
                    else -> b.append(c)
                }
            }
        }
    }
}
