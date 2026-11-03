# Prior-art and dependency audit

This audit records design references, not permission to reuse implementation code. CKey's geometry, framing, and code in the first milestone are independently authored. Before copying or linking any implementation, inspect the exact source revision and every bundled dependency license.

| System | What is relevant to CKey | License / reuse note |
|---|---|---|
| FHNW CODEC / Annular Barcodes | Separates marker, synchronization, and data zones; reports decoding under distortions/noise. Useful precedent for explicit calibration zones and a camera test corpus. | The linked research pages do not establish a reusable source-code license. Cite the research; do not copy code absent a license. |
| CCTag | Concentric-ring fiducial localization and pose under difficult imaging. Useful for candidate localization research, not a payload format. | Official implementation is MPL-2.0. Any future source reuse needs file-level MPL compliance review. |
| RuneTag | Concentric dots and cyclic code for identity/localization with occlusion. | Official repository states MIT, but includes bundled/dependent components (including WinNTL); audit all dependency licenses before reuse. |
| AprilTag | Robust square candidate detection and deliberately small identifier code families. Useful detection benchmark, not an appropriate high-capacity model. | BSD-licensed official implementation; no code copied here. |
| Apple App Clip Code | Public platform docs establish behavior but do not disclose the symbol math; the independent reverse-engineering project describes colored arc rings, URL framing, and RS. Useful reminder to specify arc topology exactly. | Apple docs do not publish a symbology license. Reverse-engineering repository is MIT; do not copy its format tables/implementation or imply Apple affiliation. |
| HCC2D | Adds color planes to a matrix code; illustrates capacity gains versus color-channel sensitivity. | Public draft text is CC BY-ND 4.0; official software is proprietary. No text/code copied. CKey's monochrome requirement rules out color dependence. |
| PiCode | Research on embedding imagery while preserving decode behavior; informs need for explicit logo damage budgets. | No source license identified in this audit. Cite paper only; no code copied. |
| IRIS | A close radial/ring prior art with self-clocking ring cells, monochrome/color variants, and RS interleaving. Its proximity makes independent geometry and a documented comparison especially important. | Public repository states MIT; no code copied. Review exact revision before any future reuse. |

## References

* FHNW CODEC project: <https://www.fhnw.ch/plattformen/computervision/rd-projects/codecs/ring-codes/>; paper/report: <https://irf.fhnw.ch/entities/publication/5d811543-26f2-4567-bfb2-adced052c413>.
* CCTag paper: <https://openaccess.thecvf.com/content_cvpr_2016/html/Calvet_Detection_and_Accurate_CVPR_2016_paper.html>; code: <https://github.com/alicevision/CCTag>.
* RuneTag paper DOI: <https://doi.org/10.1109/CVPR.2011.5995544>; code: <https://github.com/artursg/RUNEtag>.
* AprilTag project and license: <https://april.eecs.umich.edu/software/apriltag.html>; <https://github.com/AprilRobotics/apriltag>.
* Apple App Clip Code docs: <https://developer.apple.com/documentation/appclip/creating_app_clip_codes>; independent analysis: <https://github.com/rs/appclipcode>.
* HCC2D draft: <https://hcc2d.com/en/spec>.
* PiCode paper DOI: <https://doi.org/10.1109/TIP.2016.2573592>.
* IRIS repository: <https://github.com/AllTerrainDeveloper/iris>.

No third-party runtime dependency is used by the current C++ core. The repository's existing top-level MIT license is retained. Apache-2.0 would offer an express patent grant and is a sound alternative for a new standard, but changing from MIT should be a deliberate owner decision after checking contributor rights.
