package org.openckey.ckey.compose

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Constraints
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.openckey.ckey.CKey
import org.openckey.ckey.CKeyStyle
import org.openckey.ckey.CKeySymbol
import org.openckey.ckey.EccLevel
import org.openckey.ckey.Layout
import org.openckey.ckey.RasterPolicy

/**
 * Shows a CKey for [data]. Square; rendered off the main thread as native
 * pixels at the exact pixel size laid out (no SVG, file or WebView), and
 * re-rendered when the size or any parameter changes.
 *
 * With [transparent] = true unpainted pixels are transparent and the
 * composable shows whatever is behind it: make sure that background has
 * enough contrast (see CKeySymbol.certify).
 *
 * Encoding or style failures (payload too large, style refused under
 * StylePolicy.STRICT) leave the composable empty and are passed to [onError].
 */
@Composable
fun CKeyImage(
    data: String,
    modifier: Modifier = Modifier,
    style: CKeyStyle? = null,
    level: EccLevel = EccLevel.M,
    layout: Layout = Layout.STANDARD,
    policy: RasterPolicy = RasterPolicy.COVERAGE,
    transparent: Boolean = false,
    contentDescription: String? = "CKey",
    onError: ((Throwable) -> Unit)? = null,
) {
    val context = LocalContext.current
    LaunchedEffect(Unit) { CKey.install(context) }
    val currentOnError by rememberUpdatedState(onError)
    BoxWithConstraints(modifier.defaultMinSize(192.dp, 192.dp).aspectRatio(1f)) {
        val fallback = with(LocalDensity.current) { 192.dp.roundToPx() }
        val w = if (constraints.maxWidth == Constraints.Infinity) fallback else constraints.maxWidth
        val h = if (constraints.maxHeight == Constraints.Infinity) fallback else constraints.maxHeight
        val side = minOf(w, h).coerceIn(16, 4096)
        val bitmap: ImageBitmap? by produceState<ImageBitmap?>(null, data, side, style, level, layout, policy, transparent) {
            value = withContext(Dispatchers.Default) {
                runCatching {
                    CKeySymbol.encode(data, 0, level, layout).toBitmap(side, policy, transparent, style).asImageBitmap()
                }
            }.onFailure { currentOnError?.invoke(it) }.getOrNull()
        }
        bitmap?.let {
            Image(it, contentDescription, Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
        }
    }
}
