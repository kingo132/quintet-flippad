package local.sfc.tall;

/** Shared width-first map/controls geometry, measured in physical view pixels. */
final class PortraitLayout {
    static float touchTop(float height, float density) {
        return Math.max(0, height - 212 * density);
    }
    static float scale(float width, int nativeWidth, float zoom) {
        return scale(width,nativeWidth,zoom,false);
    }
    static float scale(float width,int nativeWidth,float zoom,boolean integer){
        float value=zoom>0?Math.max(1,Math.min(6,zoom)):width/Math.max(1,nativeWidth);
        return integer?Math.max(1,(float)Math.floor(value+0.0001f)):value;
    }
    static int touchRows(int nativeWidth, float width, float height, float density) {
        return rowsForScale(touchTop(height,density),scale(width,nativeWidth,0),false);
    }
    static int rowsForScale(float available,float scale,boolean cover){
        double units=available/Math.max(.01f,scale)/8;
        int rows=(int)(cover?Math.ceil(units):Math.floor(units))*8;
        return Math.max(256,Math.min(2048,rows));
    }
    static float[][] buttons(float width, float height, float density) {
        float margin = 8 * density, gap = 5 * density;
        float top = touchTop(height, density), bottom = height - 42 * density;
        float size = Math.min(62 * density, Math.min((bottom - top) / 3, (width - 2 * margin - 2 * gap) / 6.5f));
        size = Math.max(1, size);
        float cx = margin + size * 1.5f, cy = top + size * 1.5f;
        float[][] r = new float[12][4];
        r[0] = rect(cx-size/2,cy-size*1.5f,size,size);
        r[1] = rect(cx-size/2,cy+size*.5f,size,size);
        r[2] = rect(cx-size*1.5f,cy-size/2,size,size);
        r[3] = rect(cx+size*.5f,cy-size/2,size,size);
        float right = width - margin - 3*size - 2*gap;
        float rowTop = cy - (2*size+gap)/2;
        for(int row=0;row<2;row++)for(int col=0;col<3;col++)
            r[4+row*3+col] = rect(right+col*(size+gap),rowTop+(1-row)*(size+gap),size,size);
        r[10] = new float[]{width*.38f,height-33*density,width*.62f,height-6*density};
        r[11] = new float[]{width*.70f,height-33*density,width-margin,height-6*density};
        return r;
    }
    static float[] rect(float x,float y,float width,float height){return new float[]{x,y,x+width,y+height};}
}
