using OpenCKey.Interop;

namespace OpenCKey
{
    /// <summary>
    /// The modelled acquisition used by <see cref="CKeySymbol.Certify"/>
    /// (<c>ckey_scan_model</c>). Unset properties keep the core's defaults: a
    /// phone camera at about 8 pixels per module with mild defocus, sensor
    /// noise and glare. It is a model, not a measurement.
    /// </summary>
    public sealed class CKeyScanModel
    {
        /// <summary>Camera sampling in pixels per module (default 8).</summary>
        public double? PxPerModule { get; set; }
        /// <summary>Gaussian defocus sigma in camera pixels (default 0.9).</summary>
        public double? BlurPx { get; set; }
        /// <summary>Sensor noise sigma as a fraction of full scale (default 0.03).</summary>
        public double? Noise { get; set; }
        /// <summary>Contrast compression 0..0.9 (default 0.2).</summary>
        public double? Glare { get; set; }
        /// <summary>Noise seeds per backdrop, 1..16 (default 3).</summary>
        public int? Trials { get; set; }

        internal void Apply(ref NativeScanModel m)
        {
            if (PxPerModule.HasValue) m.PxPerModule = PxPerModule.Value;
            if (BlurPx.HasValue) m.BlurPx = BlurPx.Value;
            if (Noise.HasValue) m.Noise = Noise.Value;
            if (Glare.HasValue) m.Glare = Glare.Value;
            if (Trials.HasValue) m.Trials = (uint)System.Math.Max(0, Trials.Value);
        }
    }

    /// <summary>The outcome of <see cref="CKeySymbol.Certify"/> (<c>ckey_certification</c>).</summary>
    public sealed class CKeyCertification
    {
        internal CKeyCertification(CKeyStatus status, bool certified, StylePolicy policy, StyleAdjustments adjustments,
                                   int trials, int trialsPassed, double minContrast, double featureSeparation,
                                   double logoLoad, string message, CKeyStyle? adjustedStyle)
        {
            Status = status;
            IsCertified = certified;
            Policy = policy;
            Adjustments = adjustments;
            Trials = trials;
            TrialsPassed = trialsPassed;
            MinContrast = minContrast;
            FeatureSeparation = featureSeparation;
            LogoLoad = logoLoad;
            Message = message;
            AdjustedStyle = adjustedStyle;
        }

        /// <summary>
        /// The core's status. Under <see cref="StylePolicy.Strict"/> it is
        /// <see cref="CKeyStatus.InvalidArgument"/> unless the style is certified;
        /// under <see cref="StylePolicy.Override"/> it is <see cref="CKeyStatus.Ok"/> whatever happens.
        /// </summary>
        public CKeyStatus Status { get; }
        /// <summary>True when the policy accepted the style (Status is Ok).</summary>
        public bool IsAccepted => Status == CKeyStatus.Ok;
        /// <summary>The symbol decoded in every modelled trial on every backdrop.</summary>
        public bool IsCertified { get; }
        /// <summary>The policy that was applied.</summary>
        public StylePolicy Policy { get; }
        /// <summary>Changes made by the adaptive policy.</summary>
        public StyleAdjustments Adjustments { get; }
        /// <summary>Trials run.</summary>
        public int Trials { get; }
        /// <summary>Trials that decoded the payload exactly.</summary>
        public int TrialsPassed { get; }
        /// <summary>Informative WCAG-style contrast ratio over all ink/background pairs.</summary>
        public double MinContrast { get; }
        /// <summary>Luma separation of mark vs gap pixels after compositing (0..1; &lt;= 0 overlaps).</summary>
        public double FeatureSeparation { get; }
        /// <summary>Share of the worst block's correction hidden by the logo.</summary>
        public double LogoLoad { get; }
        /// <summary>The reason for refusal, or the list of changes and warnings.</summary>
        public string Message { get; }
        /// <summary>Under the adaptive policy, the repaired style to render; otherwise null.</summary>
        public CKeyStyle? AdjustedStyle { get; }

        /// <inheritdoc />
        public override string ToString() => Message;
    }
}
