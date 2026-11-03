package local.sfc.tall;
/** Geometry checks independent of Android rendering and device input. */
public final class PortraitLayoutCheck {
    static int count;
    static void check(boolean value,String msg){if(!value)throw new AssertionError(msg);count++;}
    public static void main(String[] args){
        float[][] screens={{1080,2424,2.625f},{1080,2400,2.625f},{720,1600,2},{1440,3120,3.5f},{1200,2000,2},{480,1000,1.25f},{2160,4848,5.25f}};
        for(float[] s:screens){float w=s[0],h=s[1],density=s[2];
            for(int nativeWidth:new int[]{256,320}){
                float scale=PortraitLayout.scale(w,nativeWidth,0);
                check(Math.abs(scale*nativeWidth-w)<.01f,"auto fills physical width");
                int rows=PortraitLayout.touchRows(nativeWidth,w,h,density);
                check(rows>=256&&rows<=576&&(rows&7)==0,"bounded tile-aligned canvas");
                check(rows*scale<=PortraitLayout.touchTop(h,density)+.01f,"auto canvas above controls");
            }
            float[][] rects=PortraitLayout.buttons(w,h,density);
            for(float[] r:rects){check(r[0]>=0&&r[1]>=0&&r[2]<=w+.01&&r[3]<=h+.01,"buttons in screen");check(r[0]<r[2]&&r[1]<r[3],"positive key area");check(r[1]>=PortraitLayout.touchTop(h,density)-.01,"keys below map");}
            for(int i=0;i<rects.length;i++)for(int j=i+1;j<rects.length;j++){
                float[] a=rects[i],b=rects[j];check(Math.min(a[2],b[2])<=Math.max(a[0],b[0])+.01f||Math.min(a[3],b[3])<=Math.max(a[1],b[1])+.01f,"touch keys do not overlap");
            }
        }
        check(PortraitLayout.touchRows(256,1080,2424,2.625f)==440,"Pixel 9 H32");
        check(PortraitLayout.touchRows(320,1080,2424,2.625f)==552,"Pixel 9 H40");
        check(PortraitLayout.scale(1080,256,0,true)==4,"H32 integer auto");
        check(PortraitLayout.scale(1080,320,0,true)==3,"H40 integer auto");
        check(PortraitLayout.scale(1080,256,5,false)==5,"manual zoom is not clamped to screen fit");
        check(PortraitLayout.scale(1080,256,3.75f,false)==3.75f,"fractional zoom");
        check(PortraitLayout.scale(1080,256,3.75f,true)==3,"integer quantization");
        check((1080-PortraitLayout.scale(1080,256,5,false)*256)/2==-100,"oversized manual zoom crops symmetrically");
        check(PortraitLayout.rowsForScale(1867.5f,4,false)==464,"integer touch viewport uses remaining height");
        check(PortraitLayout.rowsForScale(1646,6,true)==280,"manual zoom keeps native view vertically centered");
        check(PortraitLayout.rowsForScale(1646,1,true)==1648,"Pixel9 1x genuine tall rows");
        check(PortraitLayout.rowsForScale(9000,1,true)==2048,"safe maximum height");
        System.out.println("portrait geometry checks passed: "+count);
    }
}
