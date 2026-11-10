using System;

namespace OpenCKey
{
    /// <summary>A call into the CKey core failed.</summary>
    public sealed class CKeyException : Exception
    {
        /// <summary>Creates the exception.</summary>
        public CKeyException(CKeyStatus status, string message) : base(message)
        {
            Status = status;
        }

        /// <summary>The core's status code.</summary>
        public CKeyStatus Status { get; }
    }
}
