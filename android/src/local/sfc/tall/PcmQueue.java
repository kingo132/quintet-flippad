package local.sfc.tall;
import android.media.AudioTrack;
/** Bounded stereo PCM pending a non-blocking AudioTrack write. */
final class PcmQueue {
    private final short[] data;
    private int head,count;
    PcmQueue(int capacity){data=new short[capacity&~1];}
    int size(){return count;}
    void clear(){head=count=0;}
    boolean offer(short[] source,int length){
        length&=~1;
        boolean fits=length<=data.length-count;
        if(length>data.length){clear();return false;}
        int discard=Math.max(0,count+length-data.length);
        head=(head+discard)%data.length;count-=discard;
        int tail=(head+count)%data.length,first=Math.min(length,data.length-tail);
        System.arraycopy(source,0,data,tail,first);
        System.arraycopy(source,first,data,0,length-first);count+=length;
        return fits;
    }
    int drain(AudioTrack track){
        while(count>0){
            int n=track.write(data,head,Math.min(count,data.length-head),AudioTrack.WRITE_NON_BLOCKING);
            if(n<0)return n;
            if(n==0)break;
            head=(head+n)%data.length;count-=n;
        }
        return 0;
    }
}
