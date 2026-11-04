# CKey Payload Conventions (informative, draft 0.1)

A CKey payload is bytes. No format reserves a content-type field. This
document says how a **reader** should interpret those bytes. It is not part
of any wire format, and a payload that follows no convention is still a
valid CKey.

## Text

Readers should treat a payload as text when it is valid UTF-8 and contains no
control characters other than tab and line feed (CR LF is accepted). Readers
show other payloads as binary, for example as hexadecimal.

## Recognized forms

A text payload whose first `:`-delimited token is one of the schemes below
should be offered as an action. Schemes are matched case-insensitively, and
leading and trailing whitespace is ignored. The first five rows follow
existing standards, so phones and other code readers already understand them.

| Form | Standard | Example | Reader action |
|---|---|---|---|
| `https:` / `http:` | RFC 3986 | `https://citeory.com/tour` | Open in the browser (show the host first) |
| `mailto:` | RFC 6068 | `mailto:hello@citeory.com?subject=Hi` | Compose an email |
| `tel:` | RFC 3966 | `tel:+15551234567` | Offer to call |
| `sms:` | RFC 5724 | `sms:+15551234567?body=Hi` | Compose a message |
| `geo:` | RFC 5870 | `geo:37.33490,-122.00900?q=Apple%20Park` | Show the coordinates in a map app |
| `address:` | **CKey convention** (below) | `address:1 Infinite Loop, Cupertino, CA 95014` | Show the address in a map app |
| `BEGIN:VCARD` … `END:VCARD` | RFC 6350 (vCard 3.0/4.0) | a complete vCard | Offer to add the contact |

**Phone numbers:** encode them in E.164 international form (`+` and country code). The number then carries its country, and no per-country form is needed.

### `address:` (CKey convention)

`address:` is followed by a free-text postal address in UTF-8. There is no percent-encoding. Line breaks may separate address lines; readers join non-empty lines with `", "`.

The scheme exists because `geo:` requires coordinates, and no standard URI carries a plain street address. `address:` is not a registered URI scheme, so non-CKey code readers will show it as text, which still degrades usefully.

The reader hands the address text to the platform's map application, which geocodes it. On iOS that is a `https://maps.apple.com/?address=…` link, routed by the system to Maps; iOS has no public API for choosing a different map application. `geo:` coordinates use `?ll=lat,lon`, plus `q=` for the label when one is present.

## Safety rules

1. **Never act automatically.** A decode only displays the payload and its proposed action, and the user must tap to proceed. The action states its target, for example "Open citeory.com", "Call +1 555…" or "Email hello@…".
2. **Allowlist only.** Offer actions only for the forms above. Treat `javascript:`, `data:`, `file:`, `intent:`, custom application schemes, and anything else as plain text with no action.
3. **Keep the payload visible.** The decoded text, and the raw bytes on request, remain visible next to any action.
4. **Validate before offering.**
   - `mailto:` must contain an `@`.
   - `tel:` and `sms:` need at least three digits; only `+` and digits are kept.
   - `geo:` latitude must be within ±90 and longitude within ±180.
   - `address:` must be non-empty after trimming.
   - `http(s):` must have a host.

## Capacity

Profile 1 carries exactly 16 bytes and is a locator, not a text carrier. Use Profile 0, which picks the smallest of 31 sizes that fits (27 bytes to 4,619 bytes at level M), or Profile 2 (up to 200 bytes) or Profile 3 (up to 2,048 bytes) for these forms. The scheme prefix costs about as much as any custom prefix would: `tel:` is 4 bytes, `address:` 8, `https://` 8.
