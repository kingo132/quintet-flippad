#!/usr/bin/env python3
"""Exercise real PCM queue against a sink with short writes and backpressure."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'android/media').mkdir(parents=True);(p/'local/sfc/tall').mkdir(parents=True)
 (p/'android/media/AudioTrack.java').write_text('''package android.media;
import java.util.*;
public class AudioTrack {
 public static final int WRITE_NON_BLOCKING=1;
 public int quota,error;public final ArrayList<Short> heard=new ArrayList<>();
 public int write(short[] p,int at,int n,int mode){if(mode!=1)throw new AssertionError("Blocking write");if(error!=0)return error;int take=Math.min(n,quota);for(int i=0;i<take;i++)heard.add(p[at+i]);quota-=take;return take;}
}''')
 (p/'local/sfc/tall/PcmQueue.java').write_text((root/'android/src/local/sfc/tall/PcmQueue.java').read_text())
 (p/'local/sfc/tall/QueueCheck.java').write_text('''package local.sfc.tall;
import android.media.AudioTrack;import java.util.*;
public class QueueCheck {
 public static void main(String[] args){
  PcmQueue q=new PcmQueue(512);AudioTrack sink=new AudioTrack();Random random=new Random(7);int sent=0;
  for(int k=0;k<1000;k++){
   int n=2+random.nextInt(60)*2;short[] data=new short[n];for(int i=0;i<n;i++)data[i]=(short)(sent+i);
   if(!q.offer(data,n))throw new AssertionError("Unexpected overflow");sent+=n;
   sink.quota=random.nextInt(30)*2;q.drain(sink); // partial write, then no space
   if(q.size()>250){sink.quota=512;q.drain(sink);}
  }
  sink.quota=512;q.drain(sink);if(sink.heard.size()!=sent)throw new AssertionError("Lost PCM");
  for(int i=0;i<sent;i++)if(sink.heard.get(i)!=(short)i)throw new AssertionError("Reordered PCM at "+i);
  q.offer(new short[]{1,2},2);sink.error=-6;if(q.drain(sink)!=-6||q.size()!=2)throw new AssertionError("Error handling");q.clear();if(q.size()!=0)throw new AssertionError("Stale PCM");
  q=new PcmQueue(8);q.offer(new short[]{0,1,2,3,4,5},6);sink=new AudioTrack();sink.quota=2;q.drain(sink);
  if(q.offer(new short[]{6,7,8,9,10,11},6))throw new AssertionError("Missing overflow report");sink.quota=8;q.drain(sink);
  if(!sink.heard.equals(Arrays.asList((short)0,(short)1,(short)4,(short)5,(short)6,(short)7,(short)8,(short)9,(short)10,(short)11)))throw new AssertionError("Overflow order");
  System.out.println("PCM partial writes, backpressure, ring wrap, reset and failure recovery passed: "+sent+" samples");
 }
}''')
 subprocess.run(['javac','-d',d,*map(str,p.rglob('*.java'))],check=True)
 subprocess.run(['java','-cp',d,'local.sfc.tall.QueueCheck'],check=True)
