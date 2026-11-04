// GPL-3.0-or-later. Generic image-only display effects; no emulated state access.
package local.sfc.tall;
import android.graphics.*;
final class VideoEffects {
    static {System.loadLibrary("tallfilters");}
    static native boolean nativeXbrz(Bitmap source,Bitmap destination,int width,int height,int factor,boolean fresh);
    static final String[] NAMES={"原始像素","平滑","扫描线","灰度","xBRZ 2×","xBRZ 3×"};
    final Paint bitmapPaint=new Paint(),scanPaint=new Paint();
    final ColorMatrixColorFilter gray;
    Bitmap filtered;
    VideoEffects(){ColorMatrix matrix=new ColorMatrix();matrix.setSaturation(0);gray=new ColorMatrixColorFilter(matrix);scanPaint.setColor(0x50000000);}
    void draw(Canvas canvas,Bitmap bitmap,int width,int height,RectF bounds,int effect){
        Bitmap image=bitmap;int sourceWidth=width,sourceHeight=height;
        if(effect==4||effect==5){
            int factor=effect==4?2:3;
            boolean fresh=filtered==null||filtered.getWidth()!=width*factor||filtered.getHeight()!=height*factor;
            if(fresh){if(filtered!=null)filtered.recycle();filtered=Bitmap.createBitmap(width*factor,height*factor,Bitmap.Config.ARGB_8888);}
            if(!nativeXbrz(bitmap,filtered,width,height,factor,fresh))throw new IllegalStateException("xBRZ 处理失败");
            image=filtered;sourceWidth=width*factor;sourceHeight=height*factor;
        }
        bitmapPaint.setFilterBitmap(effect==1);bitmapPaint.setColorFilter(effect==3?gray:null);
        canvas.drawBitmap(image,new Rect(0,0,sourceWidth,sourceHeight),bounds,bitmapPaint);
        if(effect==2){canvas.save();canvas.clipRect(bounds);float line=bounds.height()/height;for(float y=bounds.top;y<bounds.bottom;y+=line)canvas.drawRect(bounds.left,y+line*.60f,bounds.right,y+line,scanPaint);canvas.restore();}
    }
    void close(){if(filtered!=null){filtered.recycle();filtered=null;}}
}
