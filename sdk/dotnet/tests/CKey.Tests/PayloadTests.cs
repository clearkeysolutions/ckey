using System.Text;
using Xunit;

namespace OpenCKey.Tests
{
    public class PayloadTests
    {
        private static string? Text(string s) => CKeyPayload.ReadableText(Encoding.UTF8.GetBytes(s));

        [Fact]
        public void ReadableText()
        {
            Assert.Equal("hello", Text("hello"));
            Assert.Equal("a\tb\r\nc", Text("a\tb\r\nc"));
            Assert.Null(Text("bell\u0007"));
            Assert.Null(CKeyPayload.ReadableText(new byte[] { 0xc3, 0x28 })); // invalid UTF-8
            Assert.Null(CKeyPayload.ReadableText(new byte[0]));
            Assert.Equal("Grüße", Text("Grüße"));
        }

        [Theory]
        [InlineData("https://citeory.com/tour", CKeyActionKind.Web, "Open citeory.com", "https://citeory.com/tour")]
        [InlineData("  HTTP://Example.com/x  ", CKeyActionKind.Web, "Open example.com", "HTTP://Example.com/x")]
        [InlineData("mailto:hello@citeory.com?subject=Hi", CKeyActionKind.Email, "Email hello@citeory.com", "mailto:hello@citeory.com?subject=Hi")]
        [InlineData("tel:+1 (555) 123-4567", CKeyActionKind.Call, "Call +15551234567", "tel:+15551234567")]
        [InlineData("sms:+15551234567?body=Hi", CKeyActionKind.Message, "Message +15551234567", "sms:+15551234567?body=Hi")]
        [InlineData("geo:37.33490,-122.00900?q=Apple%20Park", CKeyActionKind.Map, "Show Apple Park on a map", "geo:37.33490,-122.00900?q=Apple%20Park")]
        [InlineData("geo:1.5,2.25", CKeyActionKind.Map, "Show 1.50000, 2.25000 on a map", "geo:1.5,2.25")]
        public void RecognizedForms(string text, CKeyActionKind kind, string label, string uri)
        {
            CKeyPayloadAction? action = CKeyPayload.Classify(text);
            Assert.NotNull(action);
            Assert.Equal(kind, action!.Kind);
            Assert.Equal(label, action.Label);
            Assert.Equal(uri, action.Uri);
        }

        [Fact]
        public void Address()
        {
            CKeyPayloadAction? action = CKeyPayload.Classify("address:1 Infinite Loop\n Cupertino, CA 95014 \n\n");
            Assert.Equal(CKeyActionKind.Map, action!.Kind);
            Assert.Equal("1 Infinite Loop, Cupertino, CA 95014", action.Target);
            Assert.Null(action.Uri);
            Assert.Null(CKeyPayload.Classify("address:   \n "));
        }

        [Fact]
        public void VCard()
        {
            const string card = "BEGIN:VCARD\r\nVERSION:4.0\r\nFN:Ada Lovelace\r\nEND:VCARD";
            CKeyPayloadAction? action = CKeyPayload.Classify(card);
            Assert.Equal(CKeyActionKind.Contact, action!.Kind);
            Assert.Equal("Ada Lovelace", action.Target);
            Assert.Equal(card, action.VCard);
            Assert.Null(CKeyPayload.Classify("BEGIN:VCARD\nFN:Incomplete"));
        }

        [Theory]
        [InlineData("javascript:alert(1)")]
        [InlineData("data:text/html,<b>x</b>")]
        [InlineData("file:///etc/passwd")]
        [InlineData("intent://scan/#Intent;scheme=zxing;end")]
        [InlineData("myapp://open")]
        [InlineData("just some text")]
        [InlineData("mailto:nobody")]
        [InlineData("tel:12")]
        [InlineData("geo:91,0")]
        [InlineData("geo:0,181")]
        [InlineData("geo:abc,def")]
        [InlineData("https://")]
        [InlineData("")]
        public void EverythingElseIsPlainText(string text)
        {
            Assert.Null(CKeyPayload.Classify(text));
        }

        [Fact]
        public void NullIsPlainText() => Assert.Null(CKeyPayload.Classify(null));
    }
}
