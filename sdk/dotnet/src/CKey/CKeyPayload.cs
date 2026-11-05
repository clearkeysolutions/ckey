using System;
using System.Globalization;
using System.Text;

namespace OpenCKey
{
    /// <summary>What a recognized text payload offers (spec/CKey-Payload-Conventions.md).</summary>
    public enum CKeyActionKind
    {
        /// <summary>https: / http: — open in the browser.</summary>
        Web,
        /// <summary>mailto: — compose an email.</summary>
        Email,
        /// <summary>tel: — offer to call.</summary>
        Call,
        /// <summary>sms: — compose a message.</summary>
        Message,
        /// <summary>geo: or address: — show in a map application.</summary>
        Map,
        /// <summary>BEGIN:VCARD … END:VCARD — offer to add the contact.</summary>
        Contact,
    }

    /// <summary>
    /// A proposed action for a decoded payload. It is only a proposal: a host
    /// must show it with the payload and act only when the user confirms
    /// (safety rule 1: never act automatically).
    /// </summary>
    public sealed class CKeyPayloadAction
    {
        internal CKeyPayloadAction(CKeyActionKind kind, string label, string target, string? uri,
                                   double? latitude = null, double? longitude = null, string? vCard = null)
        {
            Kind = kind;
            Label = label;
            Target = target;
            Uri = uri;
            Latitude = latitude;
            Longitude = longitude;
            VCard = vCard;
        }

        /// <summary>The kind of action.</summary>
        public CKeyActionKind Kind { get; }
        /// <summary>A label that states the target, e.g. "Open example.com" or "Call +15551234567".</summary>
        public string Label { get; }
        /// <summary>The target: host, email address, phone number, map label/address, or contact name.</summary>
        public string Target { get; }
        /// <summary>
        /// A URI the host may hand to the OS after the user confirms (web, mailto,
        /// tel, sms and geo forms); null for address: and vCards, which the host
        /// routes to its own map or contacts integration.
        /// </summary>
        public string? Uri { get; }
        /// <summary>geo: latitude.</summary>
        public double? Latitude { get; }
        /// <summary>geo: longitude.</summary>
        public double? Longitude { get; }
        /// <summary>The complete vCard text for <see cref="CKeyActionKind.Contact"/>.</summary>
        public string? VCard { get; }

        /// <inheritdoc />
        public override string ToString() => Label;
    }

    /// <summary>Payload interpretation helpers (spec/CKey-Payload-Conventions.md).</summary>
    public static class CKeyPayload
    {
        private static readonly UTF8Encoding StrictUtf8 = new UTF8Encoding(false, true);

        /// <summary>
        /// The payload as text when it is valid UTF-8 with no control characters
        /// other than tab, LF and CR; otherwise null (show it as binary/hex).
        /// An empty payload is not text.
        /// </summary>
        public static string? ReadableText(ReadOnlySpan<byte> payload)
        {
            if (payload.IsEmpty) return null;
            string text;
            try
            {
                text = DecodeStrict(payload);
            }
            catch (DecoderFallbackException)
            {
                return null;
            }
            foreach (char ch in text)
            {
                if (ch == '\t' || ch == '\n' || ch == '\r') continue;
                if (char.IsControl(ch)) return null;
            }
            return text;
        }

        private static unsafe string DecodeStrict(ReadOnlySpan<byte> payload)
        {
            fixed (byte* p = payload) return StrictUtf8.GetString(p, payload.Length);
        }

        /// <summary>Lower-case hexadecimal of the payload bytes.</summary>
        public static string ToHex(ReadOnlySpan<byte> payload)
        {
            var sb = new StringBuilder(payload.Length * 2);
            foreach (byte b in payload) sb.Append(b.ToString("x2", CultureInfo.InvariantCulture));
            return sb.ToString();
        }

        /// <summary>
        /// Classifies a text payload into a proposed action, or null for plain
        /// text. Only the allowlisted forms are recognized (https/http, mailto,
        /// tel, sms, geo, address, vCard); javascript:, data:, file:, intent:,
        /// custom schemes and anything else yield null. This never acts.
        /// </summary>
        public static CKeyPayloadAction? Classify(string? text)
        {
            if (text == null) return null;
            string trimmed = text.Trim();
            if (trimmed.Length == 0) return null;
            if (trimmed.StartsWith("BEGIN:VCARD", StringComparison.OrdinalIgnoreCase)) return ContactCard(trimmed);
            int colon = trimmed.IndexOf(':');
            if (colon <= 0) return null;
            string scheme = trimmed.Substring(0, colon).ToLowerInvariant();
            string rest = trimmed.Substring(colon + 1);
            int q = rest.IndexOf('?');
            string beforeQuery = q >= 0 ? rest.Substring(0, q) : rest;
            string query = q >= 0 ? rest.Substring(q + 1) : string.Empty;
            switch (scheme)
            {
                case "http":
                case "https":
                {
                    if (!System.Uri.TryCreate(trimmed, UriKind.Absolute, out Uri? uri) || string.IsNullOrEmpty(uri.Host)) return null;
                    if (uri.Scheme != System.Uri.UriSchemeHttp && uri.Scheme != System.Uri.UriSchemeHttps) return null;
                    return new CKeyPayloadAction(CKeyActionKind.Web, "Open " + uri.Host, uri.Host, trimmed);
                }
                case "mailto":
                {
                    string address = Unescape(beforeQuery);
                    if (address.IndexOf('@') < 0) return null;
                    return new CKeyPayloadAction(CKeyActionKind.Email, "Email " + address, address, trimmed);
                }
                case "tel":
                case "sms":
                {
                    // E.164 is recommended (tel:+15551234567); keep only dialable characters.
                    var number = new StringBuilder();
                    int digits = 0;
                    foreach (char ch in beforeQuery)
                    {
                        if (ch >= '0' && ch <= '9') { number.Append(ch); digits++; }
                        else if (ch == '+') number.Append(ch);
                    }
                    if (digits < 3) return null;
                    string n = number.ToString();
                    return scheme == "tel"
                        ? new CKeyPayloadAction(CKeyActionKind.Call, "Call " + n, n, "tel:" + n)
                        : new CKeyPayloadAction(CKeyActionKind.Message, "Message " + n, n, trimmed);
                }
                case "geo":
                {
                    // RFC 5870: geo:lat,lon[,alt][;params][?q=label]
                    string[] coordinates = beforeQuery.Split(';')[0].Split(',');
                    if (coordinates.Length < 2) return null;
                    if (!double.TryParse(coordinates[0], NumberStyles.Float, CultureInfo.InvariantCulture, out double lat) ||
                        !double.TryParse(coordinates[1], NumberStyles.Float, CultureInfo.InvariantCulture, out double lon)) return null;
                    if (!(lat >= -90 && lat <= 90) || !(lon >= -180 && lon <= 180)) return null;
                    string? label = null;
                    foreach (string item in query.Split('&'))
                    {
                        if (item.StartsWith("q=", StringComparison.Ordinal)) { label = Unescape(item.Substring(2)); break; }
                    }
                    if (string.IsNullOrEmpty(label))
                        label = string.Format(CultureInfo.InvariantCulture, "{0:F5}, {1:F5}", lat, lon);
                    return new CKeyPayloadAction(CKeyActionKind.Map, $"Show {label} on a map", label!, trimmed, lat, lon);
                }
                case "address":
                {
                    // CKey convention: free-text postal address (UTF-8, no
                    // percent-encoding); line breaks separate address lines.
                    var lines = rest.Split(new[] { "\r\n", "\n", "\r", "\u0085", " ", " " }, StringSplitOptions.None);
                    var sb = new StringBuilder();
                    foreach (string line in lines)
                    {
                        string t = line.Trim();
                        if (t.Length == 0) continue;
                        if (sb.Length > 0) sb.Append(", ");
                        sb.Append(t);
                    }
                    if (sb.Length == 0) return null;
                    string address = sb.ToString();
                    return new CKeyPayloadAction(CKeyActionKind.Map, $"Show {address} on a map", address, null);
                }
                default:
                    return null;
            }
        }

        private static CKeyPayloadAction? ContactCard(string card)
        {
            if (card.IndexOf("END:VCARD", StringComparison.OrdinalIgnoreCase) < 0) return null;
            string name = "contact";
            foreach (string raw in card.Split(new[] { "\r\n", "\n", "\r" }, StringSplitOptions.None))
            {
                string line = raw.Trim();
                if (line.StartsWith("FN:", StringComparison.OrdinalIgnoreCase) || line.StartsWith("FN;", StringComparison.OrdinalIgnoreCase))
                {
                    int c = line.IndexOf(':');
                    if (c >= 0 && c + 1 < line.Length) name = line.Substring(c + 1).Trim();
                    break;
                }
            }
            if (name.Length == 0) name = "contact";
            return new CKeyPayloadAction(CKeyActionKind.Contact, $"Add {name} to contacts", name, null, vCard: card);
        }

        private static string Unescape(string s)
        {
            try
            {
                return System.Uri.UnescapeDataString(s);
            }
            catch (UriFormatException)
            {
                return s;
            }
        }
    }
}
