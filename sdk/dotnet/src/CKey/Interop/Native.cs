using System;
using System.Text;

namespace OpenCKey.Interop
{
    /// <summary>Small helpers shared by the wrappers: ABI check, status checks, C strings.</summary>
    internal static unsafe class Native
    {
        private static readonly object Gate = new object();
        private static volatile bool _abiChecked;

        /// <summary>
        /// Verifies, once, that the loaded native library speaks the ABI these
        /// struct mirrors were written for. A mismatched library would read
        /// the structs at the wrong offsets, so it is refused outright.
        /// </summary>
        internal static void EnsureAbi()
        {
            if (_abiChecked) return;
            lock (Gate)
            {
                if (_abiChecked) return;
                uint abi;
                try
                {
                    abi = NativeMethods.ckey_get_abi_version();
                }
                catch (DllNotFoundException e)
                {
                    throw new CKeyException(CKeyStatus.InternalError,
                        "The native ckey library was not found. The CKey package ships it for win-x64, linux-x64, " +
                        "osx-x64 and osx-arm64; for other platforms build sdk/dotnet/native with CMake. " + e.Message);
                }
                if (abi != NativeConstants.AbiVersion)
                {
                    throw new CKeyException(CKeyStatus.InternalError,
                        $"The native ckey library has ABI version {abi}; this CKey assembly requires {NativeConstants.AbiVersion}.");
                }
                _abiChecked = true;
            }
        }

        internal static void Check(int status, string what)
        {
            if (status != 0) throw new CKeyException((CKeyStatus)status, $"{what} failed: {(CKeyStatus)status}.");
        }

        internal static void Check(int status, string what, ref NativeDiagnostics diagnostics)
        {
            if (status == 0) return;
            string message = Message(ref diagnostics);
            throw new CKeyException((CKeyStatus)status,
                string.IsNullOrEmpty(message) ? $"{what} failed: {(CKeyStatus)status}." : $"{what} failed: {message}");
        }

        internal static string Message(ref NativeDiagnostics diagnostics)
        {
            fixed (byte* m = diagnostics.Message) return FromUtf8Z(m, NativeConstants.DiagnosticsMessageBytes) ?? string.Empty;
        }

        /// <summary>Reads a NUL-terminated UTF-8 string of at most max bytes; null for a null pointer.</summary>
        internal static string? FromUtf8Z(byte* p, int max = int.MaxValue)
        {
            if (p == null) return null;
            int n = 0;
            while (n < max && p[n] != 0) n++;
            return n == 0 ? string.Empty : Encoding.UTF8.GetString(p, n);
        }

        /// <summary>UTF-8 bytes with a terminating NUL; null for null.</summary>
        internal static byte[]? ToUtf8Z(string? s)
        {
            if (s == null) return null;
            int n = Encoding.UTF8.GetByteCount(s);
            var bytes = new byte[n + 1];
            Encoding.UTF8.GetBytes(s, 0, s.Length, bytes, 0);
            return bytes;
        }

        internal static int ToInt32Length(nuint length, string what)
        {
            if (length > int.MaxValue) throw new CKeyException(CKeyStatus.InsufficientBuffer, $"{what} needs more than 2 GiB.");
            return (int)length;
        }
    }
}
