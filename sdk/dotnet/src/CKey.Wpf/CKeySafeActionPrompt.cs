using System;
using System.Threading.Tasks;
using System.Windows;

namespace OpenCKey.Wpf
{
    /// <summary>
    /// The SDK's WPF confirmation for a cross-device Safe Action
    /// (spec/CKey-Safe-Actions.md, "What a reader must do", step 6).
    /// </summary>
    /// <remarks>
    /// This is the only WPF way to approve a <see cref="CKeySafeActionRequest"/>: it names the
    /// verified issuer and the issuer's account of who is asking, and the
    /// registered handler runs only when the person chooses Yes. No, closing the
    /// dialog and Escape decline. Call on the UI thread.
    /// </remarks>
    public static class CKeySafeActionPrompt
    {
        /// <summary>Shows the confirmation. Returns true when the person approved and the handler ran.</summary>
        public static Task<bool> ShowAsync(Window? owner, CKeySafeActionRequest request)
        {
            if (request == null) throw new ArgumentNullException(nameof(request));
            if (request.IsDecided) return Task.FromResult(false);
            string text = request.Message + "\n\nContinue?";
            MessageBoxResult answer = owner != null
                ? MessageBox.Show(owner, text, request.Title, MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No)
                : MessageBox.Show(text, request.Title, MessageBoxButton.YesNo, MessageBoxImage.Warning, MessageBoxResult.No);
            return request.Decide(answer == MessageBoxResult.Yes);
        }
    }
}
