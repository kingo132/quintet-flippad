package local.sfc.tall;
import android.graphics.Path;
/** Same physical boundary used by the earlier Tengai/Tenchi FlipPad ports.
 * All dimensions are reference pixels at 1080px width, independent of density. */
final class FlipPadGeometry {
    static float bottom(float width, float height, int offset) {
        return Math.min(height, Math.round(height * 2f / 3f) - Math.round((50 - offset) * width / 1080f));
    }
    static float edgeY(float x, float width, float height, int offset) {
        float radius = 200 * width / 1080f, tangent = 160 * width / 1080f;
        float d = Math.max(0, tangent - Math.min(x, width - x));
        return bottom(width, height, offset) + radius - (float)Math.sqrt(Math.max(0, radius * radius - d * d));
    }
    static Path visible(float width, float height, int offset) {
        Path p = new Path(); p.moveTo(0, 0); p.lineTo(width, 0);
        for (float x = width; x >= 0; x -= 2) p.lineTo(x, edgeY(x, width, height, offset));
        p.lineTo(0, edgeY(0, width, height, offset)); p.close(); return p;
    }
}
