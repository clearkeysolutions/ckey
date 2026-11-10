package org.openckey.ckey

import java.net.URLDecoder
import java.net.URLEncoder
import java.nio.ByteBuffer
import java.nio.charset.CharacterCodingException
import java.nio.charset.CodingErrorAction

/**
 * An action a decoded text payload offers (spec/CKey-Payload-Conventions.md).
 *
 * The SDK never acts on a payload. Show the payload and [label] ("Open
 * example.com", "Call +1555…") and let the user tap; then hand [uri] to an
 * `Intent(Intent.ACTION_VIEW, Uri.parse(uri))` (or ACTION_SENDTO / ACTION_DIAL,
 * see each type). Only the allowlisted forms below ever produce an action;
 * `javascript:`, `data:`, `file:`, `intent:` and every other scheme are plain
 * text.
 */
sealed class CKeyPayloadAction {
    /** What the action targets, phrased for a button. */
    abstract val label: String
    /** A URI an Android intent can open, or null when there is none (vCard). */
    abstract val uri: String?

    /** `http:`/`https:`; [host] is the host the link really goes to (after any `user@`). */
    data class Web(override val uri: String, val host: String) : CKeyPayloadAction() {
        override val label get() = "Open $host"
    }

    /** `mailto:` (use ACTION_SENDTO). */
    data class Email(override val uri: String, val address: String) : CKeyPayloadAction() {
        override val label get() = "Email $address"
    }

    /** `tel:` with only `+` and digits kept (use ACTION_DIAL, never ACTION_CALL). */
    data class Call(override val uri: String, val number: String) : CKeyPayloadAction() {
        override val label get() = "Call $number"
    }

    /** `sms:` (use ACTION_SENDTO); [body] from `?body=` if present. */
    data class Message(override val uri: String, val number: String, val body: String?) : CKeyPayloadAction() {
        override val label get() = "Message $number"
    }

    /** `geo:` coordinates, as an Android `geo:lat,lon?q=lat,lon(label)` URI. */
    data class Location(override val uri: String, val latitude: Double, val longitude: Double, val name: String?) :
        CKeyPayloadAction() {
        override val label get() = "Show ${name ?: "%.5f, %.5f".format(java.util.Locale.ROOT, latitude, longitude)} in Maps"
    }

    /** `address:` (CKey convention), as an Android `geo:0,0?q=<address>` URI for the map app to geocode. */
    data class Address(override val uri: String, val address: String) : CKeyPayloadAction() {
        override val label get() = "Show $address in Maps"
    }

    /** A complete vCard; offer "Add to contacts" (e.g. share as text/x-vcard). */
    data class Contact(val vcard: String, val name: String?) : CKeyPayloadAction() {
        override val uri: String? get() = null
        override val label get() = "Add ${name ?: "contact"} to Contacts"
    }
}

/** Payload interpretation helpers (spec/CKey-Payload-Conventions.md). Pure Kotlin, no Android APIs. */
object CKeyPayload {
    /**
     * The payload as text when it is valid UTF-8 with no control characters
     * other than tab and line feed (CR is accepted only as part of CR LF);
     * null for binary payloads, which readers show as hexadecimal.
     */
    @JvmStatic
    fun text(bytes: ByteArray): String? {
        if (bytes.isEmpty()) return null
        val decoder = Charsets.UTF_8.newDecoder()
            .onMalformedInput(CodingErrorAction.REPORT)
            .onUnmappableCharacter(CodingErrorAction.REPORT)
        val text = try {
            decoder.decode(ByteBuffer.wrap(bytes)).toString()
        } catch (_: CharacterCodingException) {
            return null
        }
        for (i in text.indices) {
            val c = text[i]
            if (c == '\t' || c == '\n') continue
            if (c == '\r' && i + 1 < text.length && text[i + 1] == '\n') continue
            if (Character.getType(c) == Character.CONTROL.toInt()) return null
        }
        return text
    }

    /** Lowercase hexadecimal of [bytes]. */
    @JvmStatic
    fun hex(bytes: ByteArray): String {
        val digits = "0123456789abcdef"
        val sb = StringBuilder(bytes.size * 2)
        for (b in bytes) {
            val v = b.toInt() and 0xff
            sb.append(digits[v ushr 4]).append(digits[v and 15])
        }
        return sb.toString()
    }

    /**
     * Classifies a text payload into an allowlisted action, or null (plain
     * text, no action). Never performs the action.
     */
    @JvmStatic
    fun classify(text: String): CKeyPayloadAction? {
        val trimmed = text.trim()
        if (trimmed.isEmpty()) return null
        if (trimmed.regionMatches(0, "BEGIN:VCARD", 0, 11, ignoreCase = true)) return contact(trimmed)
        val colon = trimmed.indexOf(':')
        if (colon <= 0) return null
        val scheme = trimmed.substring(0, colon).lowercase(java.util.Locale.ROOT)
        val rest = trimmed.substring(colon + 1)
        val query = rest.indexOf('?')
        val beforeQuery = if (query >= 0) rest.substring(0, query) else rest
        val queryString = if (query >= 0) rest.substring(query + 1) else ""
        return when (scheme) {
            "http", "https" -> web(trimmed, rest)
            "mailto" -> {
                val address = percentDecode(beforeQuery)
                if (!address.contains('@')) null else CKeyPayloadAction.Email(trimmed, address)
            }
            "tel", "sms" -> {
                val number = beforeQuery.filter { it == '+' || it in '0'..'9' }
                if (number.count { it in '0'..'9' } < 3) null
                else if (scheme == "tel") CKeyPayloadAction.Call("tel:$number", number)
                else CKeyPayloadAction.Message(trimmed, number, queryParam(queryString, "body"))
            }
            "geo" -> geo(beforeQuery, queryString)
            "address" -> {
                val address = rest.split('\n', '\r').map { it.trim() }.filter { it.isNotEmpty() }.joinToString(", ")
                if (address.isEmpty()) null
                else CKeyPayloadAction.Address("geo:0,0?q=" + encode(address), address)
            }
            else -> null
        }
    }

    private fun web(whole: String, rest: String): CKeyPayloadAction? {
        // RFC 3986: "//" authority; the host is what follows any userinfo and
        // precedes the port, path, query or fragment.
        if (!rest.startsWith("//")) return null
        val authorityEnd = rest.indexOfAny(charArrayOf('/', '?', '#'), 2).let { if (it < 0) rest.length else it }
        val authority = rest.substring(2, authorityEnd)
        if (authority.any { it.isWhitespace() }) return null
        val hostPort = authority.substringAfterLast('@')
        val host = if (hostPort.startsWith("[")) {
            val close = hostPort.indexOf(']')
            if (close < 0) return null
            hostPort.substring(0, close + 1)
        } else {
            hostPort.substringBefore(':')
        }
        if (host.isEmpty() || host == "[]") return null
        return CKeyPayloadAction.Web(whole, host.lowercase(java.util.Locale.ROOT))
    }

    private val number = Regex("^[+-]?[0-9]+(\\.[0-9]+)?$")

    private fun geo(beforeQuery: String, queryString: String): CKeyPayloadAction? {
        // RFC 5870: geo:lat,lon[,alt][;params][?q=label]
        val coordinates = beforeQuery.substringBefore(';').split(',')
        if (coordinates.size < 2) return null
        val latText = coordinates[0].trim()
        val lonText = coordinates[1].trim()
        if (!number.matches(latText) || !number.matches(lonText)) return null
        val lat = latText.toDouble()
        val lon = lonText.toDouble()
        if (lat !in -90.0..90.0 || lon !in -180.0..180.0) return null
        val name = queryParam(queryString, "q")?.takeIf { it.isNotBlank() }
        val ll = "$latText,$lonText"
        val q = if (name != null) "$ll(${encode(name)})" else ll
        return CKeyPayloadAction.Location("geo:$ll?q=$q", lat, lon, name)
    }

    private fun contact(card: String): CKeyPayloadAction? {
        // Conventions: a complete vCard, BEGIN:VCARD ... END:VCARD.
        val lines = card.split("\r\n", "\n")
        if (!lines.any { it.trim().equals("END:VCARD", ignoreCase = true) }) return null
        val name = lines.firstOrNull { it.startsWith("FN:", true) || it.startsWith("FN;", true) }
            ?.substringAfter(':')?.trim()?.takeIf { it.isNotEmpty() }
        return CKeyPayloadAction.Contact(card, name)
    }

    private fun queryParam(query: String, key: String): String? {
        if (query.isEmpty()) return null
        for (part in query.split('&')) {
            val eq = part.indexOf('=')
            val k = if (eq >= 0) part.substring(0, eq) else part
            if (k.equals(key, ignoreCase = true)) return percentDecode(if (eq >= 0) part.substring(eq + 1) else "")
        }
        return null
    }

    /** Percent-decoding that leaves '+' alone (RFC 3986, unlike form encoding) and tolerates bad escapes. */
    private fun percentDecode(s: String): String = try {
        URLDecoder.decode(s.replace("+", "%2B"), "UTF-8")
    } catch (_: IllegalArgumentException) {
        s
    }

    private fun encode(s: String): String = URLEncoder.encode(s, "UTF-8").replace("+", "%20")
}
