package local.sfc.tall;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;
public class FrameExchangeCheck {
    static final class Buffer{int number;final int[] pixels=new int[256];}
    static void check(boolean ok,String message){if(!ok)throw new AssertionError(message);}
    public static void main(String[] args)throws Exception{
        List<Buffer> buffers=Arrays.asList(new Buffer(),new Buffer(),new Buffer());
        FrameExchange<Buffer> pipe=new FrameExchange<>(buffers);AtomicReference<Throwable> error=new AtomicReference<>();final int frames=12000;
        Thread producer=new Thread(()->{try{for(int i=0;i<frames;i++){Buffer b=pipe.acquire();check(b!=null,"unexpected close");b.number=i;for(int p=0;p<b.pixels.length;p++)b.pixels[p]=i^p;check(pipe.submit(b),"submit rejected");}}catch(Throwable t){error.set(t);pipe.close();}});
        Thread consumer=new Thread(()->{try{for(int i=0;i<frames;i++){Buffer b=pipe.receive();check(b!=null&&b.number==i,"frame skipped or reordered");if(i%29==0)Thread.yield();for(int p=0;p<b.pixels.length;p++)check(b.pixels[p]==(i^p),"producer overwrote a frame still being displayed");pipe.release(b);}}catch(Throwable t){error.set(t);}finally{pipe.close();}});
        producer.start();consumer.start();producer.join(10000);consumer.join(10000);check(!producer.isAlive()&&!consumer.isAlive(),"pipeline deadlock");if(error.get()!=null)throw new AssertionError(error.get());
        FrameExchange<Buffer> full=new FrameExchange<>(Collections.singletonList(new Buffer()));full.acquire();
        Thread blockedProducer=new Thread(()->{try{check(full.acquire()==null,"closed producer received a frame");}catch(InterruptedException e){throw new AssertionError(e);}});blockedProducer.setUncaughtExceptionHandler((t,e)->error.set(e));blockedProducer.start();full.close();blockedProducer.join(1000);check(!blockedProducer.isAlive(),"close did not wake producer");
        FrameExchange<Buffer> empty=new FrameExchange<>(Collections.singletonList(new Buffer()));Thread blockedConsumer=new Thread(()->{try{check(empty.receive()==null,"closed renderer received a frame");}catch(InterruptedException e){throw new AssertionError(e);}});blockedConsumer.setUncaughtExceptionHandler((t,e)->error.set(e));blockedConsumer.start();empty.close();blockedConsumer.join(1000);check(!blockedConsumer.isAlive(),"close did not wake renderer");
        if(error.get()!=null)throw new AssertionError(error.get());
        System.out.println("12000 complete frames: FIFO, no drops, no overwrites, bounded buffers, shutdown wakeup passed");
    }
}
