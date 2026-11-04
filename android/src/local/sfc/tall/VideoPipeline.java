package local.sfc.tall;
import android.graphics.*;
import android.view.SurfaceHolder;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
/** FIFO picture pipeline; the producer exclusively owns all game/audio state. */
final class VideoPipeline {
    static final class Frame {
        final Bitmap pixels=Bitmap.createBitmap(720,2048,Bitmap.Config.RGB_565);
        int width,height,screenWidth,screenHeight,effect,edge,epoch;
        float scale,available;
        boolean pad;
        long sequence,submitted,presentAt;
    }
    interface FilterFailure{void failed(RuntimeException error);}
    private final Frame[] buffers={new Frame(),new Frame(),new Frame()};
    private final FrameExchange<Frame> exchange=new FrameExchange<>(Arrays.asList(buffers));
    private final AtomicInteger owners=new AtomicInteger(2);
    private final SurfaceHolder holder;
    private final FilterFailure failure;
    private final java.util.function.IntSupplier currentEpoch;
    private final Thread renderer;
    private volatile boolean closed;
    VideoPipeline(SurfaceHolder holder,FilterFailure failure,java.util.function.IntSupplier currentEpoch){
        this.holder=holder;this.failure=failure;this.currentEpoch=currentEpoch;
        renderer=new Thread(this::render,"TallRender");renderer.start();
    }
    Frame acquire()throws InterruptedException{return exchange.acquire();}
    boolean submit(Frame frame){frame.submitted=System.nanoTime();return exchange.submit(frame);}
    void release(Frame frame){exchange.release(frame);}
    void close(){closed=true;exchange.close();renderer.interrupt();}
    void producerFinished(){close();finished();}
    void join(long millis)throws InterruptedException{renderer.join(millis);}
    private void finished(){if(owners.decrementAndGet()==0)for(Frame frame:buffers)frame.pixels.recycle();}
    private void render(){
        VideoEffects effects=new VideoEffects();boolean hardware=true;
        long measure=System.nanoTime(),last=0,worst=0,totalFilter=0,totalDraw=0,totalQueue=0,lastSequence=-1;
        int posted=0,transitions=0,errors=0;
        try{
            try{android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);}catch(SecurityException ignored){}
            while(!closed){
                Frame frame;
                try{frame=exchange.receive();}catch(InterruptedException interrupted){if(closed)break;continue;}
                if(frame==null)break;
                try{
                    if(frame.epoch!=currentEpoch.getAsInt()||!holder.getSurface().isValid()){transitions++;continue;}
                    long start=System.nanoTime();int effect=frame.effect;
                    try{effects.prepare(frame.pixels,frame.width,frame.height,effect);}
                    catch(RuntimeException error){errors++;failure.failed(error);effect=0;effects.prepare(frame.pixels,frame.width,frame.height,0);}
                    totalFilter+=System.nanoTime()-start;
                    // Keep CPU preparation off the Surface lock, then submit at a fixed
                    // display-aligned deadline. Two periods allow game/filter overlap.
                    while(!closed&&frame.epoch==currentEpoch.getAsInt()){
                        long wait=frame.presentAt-8000000L-System.nanoTime();
                        if(frame.presentAt==0||wait<=0)break;
                        java.util.concurrent.locks.LockSupport.parkNanos(wait);
                    }
                    if(closed)break;
                    if(frame.epoch!=currentEpoch.getAsInt()){transitions++;continue;}
                    Canvas canvas=null;long drawStart=System.nanoTime();boolean drew=false;
                    try{
                        if(hardware)try{canvas=holder.lockHardwareCanvas();}catch(RuntimeException unavailable){hardware=false;}
                        if(canvas==null)canvas=holder.lockCanvas();
                        if(canvas!=null){
                            if(canvas.getWidth()!=frame.screenWidth||canvas.getHeight()!=frame.screenHeight){transitions++;continue;}
                            canvas.drawColor(Color.BLACK);canvas.save();
                            if(frame.pad)canvas.clipPath(FlipPadGeometry.visible(canvas.getWidth(),canvas.getHeight(),frame.edge));
                            else canvas.clipRect(0,0,canvas.getWidth(),frame.available);
                            float left=(canvas.getWidth()-frame.width*frame.scale)/2;
                            effects.drawPrepared(canvas,new RectF(left,0,left+frame.width*frame.scale,frame.height*frame.scale),effect);
                            canvas.restore();drew=true;
                        }else transitions++;
                    }catch(RuntimeException error){errors++;android.util.Log.w("TallVideo","Surface changed",error);}
                    finally{if(canvas!=null)holder.unlockCanvasAndPost(canvas);totalDraw+=System.nanoTime()-drawStart;}
                    if(drew){
                        if(frame.sequence<=lastSequence)throw new IllegalStateException("Video frame order reversed");
                        lastSequence=frame.sequence;posted++;long now=System.nanoTime();
                        totalQueue+=now-frame.submitted;if(last!=0)worst=Math.max(worst,now-last);last=now;
                        if(now-measure>3000000000L){
                            android.util.Log.i("TallRender","fps="+posted*1e9/(now-measure)+" filter="+effect+" dimensions="+frame.width+"x"+frame.height
                                +" avgFilterMs="+totalFilter/1e6/posted+" avgDrawMs="+totalDraw/1e6/posted+" avgQueueMs="+totalQueue/1e6/posted
                                +" worstGapMs="+worst/1e6+" queued="+exchange.queued()+" transitions="+transitions+" errors="+errors);
                            posted=0;measure=now;worst=totalFilter=totalDraw=totalQueue=0;transitions=errors=0;
                        }
                    }
                }finally{exchange.release(frame);}
            }
        }finally{closed=true;exchange.close();effects.close();finished();}
    }
}
