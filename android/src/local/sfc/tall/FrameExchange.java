package local.sfc.tall;
import java.util.ArrayDeque;
import java.util.Collection;
/** Bounded FIFO buffer ownership. Open sessions never discard queued frames. */
final class FrameExchange<T> {
    private final ArrayDeque<T> free=new ArrayDeque<>(),ready=new ArrayDeque<>();
    private boolean closed;
    FrameExchange(Collection<T> buffers){free.addAll(buffers);}
    synchronized T acquire()throws InterruptedException{
        while(!closed&&free.isEmpty())wait();
        return closed?null:free.removeFirst();
    }
    synchronized boolean submit(T frame){if(closed)return false;ready.addLast(frame);notifyAll();return true;}
    synchronized T receive()throws InterruptedException{
        while(!closed&&ready.isEmpty())wait();
        return closed?null:ready.removeFirst();
    }
    synchronized void release(T frame){free.addLast(frame);notifyAll();}
    synchronized int queued(){return ready.size();}
    synchronized void close(){closed=true;notifyAll();}
}
