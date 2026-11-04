package local.sfc.tall;

import android.app.*;
import android.os.*;
import android.content.*;
import android.content.SharedPreferences;
import android.graphics.*;
import android.graphics.drawable.GradientDrawable;
import android.hardware.input.InputManager;
import android.media.*;
import android.view.*;
import android.widget.*;
import java.io.*;
import java.security.MessageDigest;
import java.text.SimpleDateFormat;
import java.util.*;

/** Standalone game host; each installed flavor has one authenticated private game. */
public class MainActivity extends Activity implements InputManager.InputDeviceListener {
    static { System.loadLibrary("retro"); System.loadLibrary("tallbridge"); }
    static native boolean boot(String dir, String rom, int height);
    static native int audioRate();
    static native void configureProfile(int profile, int width);
    static native int nativeHeight();
    static native void setSprites(boolean enabled);
    static native int step(Bitmap bitmap, short[] pcm, int buttons, float speed);
    static native int dimensions();
    static native long frameNanos();
    static native void setCanvas(int height);
    static native void setLayers(int layers);
    static native boolean state(boolean load, int slot);
    static native void flush();
    static native void resetCore();

    long playedFrames;
    volatile int surfaceWidth, surfaceHeight;
    int appliedHeight = -1;
    final Object engine = new Object();
    final Object loopWake = new Object();
    void wakeLoop(){synchronized(loopWake){loopWake.notifyAll();}}
    // One permit per display-aligned game frame. Missed permits are skipped,
    // never replayed in a burst after a slow audio/device operation.
    volatile long frameTick, outputPeriodNanos = 16666667;
    long nextVsync;
    final Choreographer.FrameCallback frameClock = new Choreographer.FrameCallback() {
        public void doFrame(long time) {
            if (!running) return;
            Display display=surface.getDisplay();
            double hz=display==null?60:display.getRefreshRate();
            if(hz<=0)hz=60;
            long nativePeriod=frameNanos();
            int divisor=Math.max(1,(int)Math.round(hz*nativePeriod/1e9));
            long aligned=Math.round(1e9*divisor/hz);
            outputPeriodNanos=Math.abs(aligned-nativePeriod)<nativePeriod*.02?aligned:nativePeriod;
            if(nextVsync==0||time-nextVsync>outputPeriodNanos*4)nextVsync=time;
            if(time+Math.round(5e8/hz)>=nextVsync){
                synchronized(loopWake){frameTick++;loopWake.notifyAll();}
                nextVsync+=outputPeriodNanos;
            }
            Choreographer.getInstance().postFrameCallback(this);
        }
    };
    final Handler ui = new Handler(Looper.getMainLooper());
    volatile int keys;
    int keyDirections, axisDirections;
    void clearKeys(){keys=0;keyDirections=axisDirections=0;}
    volatile boolean running, menuOpen, background=true, audioFocus, windowFocused;
    volatile int audioEpoch;
    AudioManager audioManager; AudioFocusRequest audioRequest;
    volatile float speed = 1;
    volatile boolean sound = true;
    volatile int videoFilter;
    volatile boolean integerScale=true;
    volatile boolean pad, controller;
    boolean ready, qaPad, qaTouch;
    boolean extraSprites=true, selectGameHeld;
    final Runnable selectHold=()->{if(!menuOpen&&!background){selectGameHeld=true;keys|=1<<2;toast("游戏 SELECT");}};
    int canvas = 1, layers, slot = 1, tab, edgeOffset;
    volatile float zoom = 0; // 0: best fit, otherwise equal x/y integer scaling.
    String rom, notice = "";
    long noticeUntil;
    Thread worker;
    SharedPreferences prefs;
    FrameLayout root;
    SurfaceView surface;
    Controls controls;
    LinearLayout card, rows;
    final ArrayList<Button> focusButtons = new ArrayList<>();
    int selected, lastHat;
    int videoSection, lastBuiltTab=-1, lastBuiltSection=-1;
    ScrollView assistantScroll;
    long lastHatTime;
    Bitmap bitmap;
    final int BG = Color.rgb(29, 48, 64), CHIP = Color.rgb(43, 64, 81), MINT = Color.rgb(137, 222, 204);

    static String digest(File file)throws Exception{MessageDigest md=MessageDigest.getInstance("SHA-256");try(InputStream in=new FileInputStream(file)){byte[] b=new byte[65536];int n;while((n=in.read(b))>0)md.update(b,0,n);}StringBuilder out=new StringBuilder();for(byte b:md.digest())out.append(String.format(Locale.ROOT,"%02x",b&255));return out.toString();}
    void pulse(int mask){closeMenu();keys|=mask;ui.postDelayed(()->keys&=~mask,120);}
    int dp(float n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    void immersive() {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams lp = getWindow().getAttributes();
        if(Build.VERSION.SDK_INT>=28)lp.layoutInDisplayCutoutMode = Build.VERSION.SDK_INT>=30?3:1;
        lp.preferredRefreshRate = 120f;
        getWindow().setAttributes(lp);
        if (Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        getWindow().setStatusBarColor(Color.TRANSPARENT);
        getWindow().setNavigationBarColor(Color.BLACK);
        if(Build.VERSION.SDK_INT>=30){android.view.WindowInsetsController c=getWindow().getInsetsController();if(c!=null){c.setSystemBarsBehavior(android.view.WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);c.hide(WindowInsets.Type.statusBars()|WindowInsets.Type.navigationBars());}}else getWindow().getDecorView().setSystemUiVisibility(5894);
    }
    @Override public void onCreate(Bundle saved) {
        super.onCreate(saved);
        setVolumeControlStream(android.media.AudioManager.STREAM_MUSIC);
        audioManager=(AudioManager)getSystemService(AUDIO_SERVICE);
        audioRequest=new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener(change->{audioFocus=change==AudioManager.AUDIOFOCUS_GAIN;if(!audioFocus)audioEpoch++;wakeLoop();},ui).build();
        immersive();
        if(Build.VERSION.SDK_INT>=33)getOnBackInvokedDispatcher().registerOnBackInvokedCallback(android.window.OnBackInvokedDispatcher.PRIORITY_DEFAULT,()->{if(menuOpen)closeMenu();else menu();});
        prefs = getSharedPreferences("layout", 0);
        edgeOffset = prefs.getInt("edgeOffset", 0);
        canvas = prefs.getInt("height", 1);
        layers = prefs.getInt("layers", 0);
        extraSprites=prefs.getBoolean("extraSprites",true);
        speed = prefs.getFloat("speed", 1);
        sound = prefs.getBoolean("sound", true);
        videoFilter = prefs.getInt("videoFilter", prefs.getBoolean("smooth", false) ? 1 : 0);
        if (videoFilter < 0 || videoFilter >= VideoEffects.NAMES.length) videoFilter = 0;
        integerScale = prefs.getBoolean("integerScale", true);
        videoSection = prefs.getInt("videoSection", 0);
        if (videoSection < 0 || videoSection > 2) videoSection = 0;
        zoom = prefs.getFloat("zoom", 0);
        slot = prefs.getInt("slot", 1);
        qaPad = BuildConfig.DEBUG && getIntent().getBooleanExtra("qa_pad", false);
        qaTouch = BuildConfig.DEBUG && getIntent().getBooleanExtra("qa_touch", false);
        root = new FrameLayout(this);
        surface = new SurfaceView(this);
        root.addView(surface);
        controls = new Controls();
        root.addView(controls, new FrameLayout.LayoutParams(-1, -1));
        setContentView(root);
        surfaceWidth = getResources().getDisplayMetrics().widthPixels;
        surfaceHeight = getResources().getDisplayMetrics().heightPixels;
        bitmap = Bitmap.createBitmap(720, 2048, Bitmap.Config.RGB_565);
        InputManager inputs = (InputManager)getSystemService(INPUT_SERVICE);
        inputs.registerInputDeviceListener(this, ui);
        refreshPad();
        try {
            File game = new File(getFilesDir(), "game.sfc");
            String expected=getString(R.string.game_sha);
            if(!game.isFile()||!digest(game).equals(expected)){
                File temp=new File(getFilesDir(),"game.tmp");
                try(InputStream in=getAssets().open("private/game.sfc");FileOutputStream out=new FileOutputStream(temp)){copyBounded(in,out,32*1024*1024);}
                if(!digest(temp).equals(expected))throw new IOException("游戏数据校验失败");
                if(!temp.renameTo(game))throw new IOException("无法保存游戏数据");
            }
            configureProfile(getResources().getInteger(R.integer.game_profile),270);
            loadRom(game.getPath(),true);
        } catch (Exception e) { toast("载入失败：" + e.getMessage()); }
        surface.getHolder().addCallback(new SurfaceHolder.Callback() {
            public void surfaceCreated(SurfaceHolder holder) { startLoop(); }
            public void surfaceChanged(SurfaceHolder h, int f, int w, int x) { surfaceWidth = w; surfaceHeight = x; appliedHeight = -1; }
            public void surfaceDestroyed(SurfaceHolder h) { stopLoop(); }
        });
    }
    void refreshPad() {
        int mode=ControllerLayout.TOUCH;
        for(int id:InputDevice.getDeviceIds()){
            InputDevice d=InputDevice.getDevice(id);if(d==null)continue;
            int sources=d.getSources();
            boolean gamepad=(sources&InputDevice.SOURCE_GAMEPAD)==InputDevice.SOURCE_GAMEPAD
                ||(sources&InputDevice.SOURCE_JOYSTICK)==InputDevice.SOURCE_JOYSTICK;
            mode=Math.max(mode,ControllerLayout.classify(d.isVirtual(),gamepad,d.getVendorId(),d.getProductId()));
        }
        if(qaPad)mode=ControllerLayout.FLIPPAD;if(qaTouch)mode=ControllerLayout.TOUCH;
        boolean changed=(mode==ControllerLayout.FLIPPAD)!=pad||(mode!=ControllerLayout.TOUCH)!=controller;
        pad=mode==ControllerLayout.FLIPPAD;controller=mode!=ControllerLayout.TOUCH;
        setRequestedOrientation(controller&&!pad?android.content.pm.ActivityInfo.SCREEN_ORIENTATION_SENSOR_LANDSCAPE:android.content.pm.ActivityInfo.SCREEN_ORIENTATION_PORTRAIT);
        clearKeys();appliedHeight=-1;
        if(changed&&ready)synchronized(engine){setCanvas(effectiveHeight());}
        if(controls!=null)controls.invalidate();
        android.util.Log.i("TallLayout","mode="+mode+" flipPad="+pad+" controller="+controller+" orientation="+getRequestedOrientation());
    }
    public void onInputDeviceAdded(int id) { refreshPad(); }
    public void onInputDeviceChanged(int id) { refreshPad(); }
    public void onInputDeviceRemoved(int id) { refreshPad(); }
    float availableHeight(float width,float height){
        return pad?FlipPadGeometry.edgeY(0,width,height,edgeOffset):controller?height:PortraitLayout.touchTop(height,getResources().getDisplayMetrics().density);
    }
    float displayScale(float width,float height,int nativeWidth){
        float value=PortraitLayout.scale(width,nativeWidth,zoom,integerScale);
        return controller&&!pad?ControllerLayout.landscapeScale(width,height,value,integerScale):value;
    }
    int effectiveHeight() {
        if(canvas==0)return 0;
        if(pad&&canvas==1&&zoom==0&&!integerScale)return 1;
        float available=availableHeight(surfaceWidth,surfaceHeight);
        float scale=displayScale(surfaceWidth,surfaceHeight,256);
        int rows=PortraitLayout.rowsForScale(available,scale,controller);
        if(controller&&!pad&&surfaceWidth>surfaceHeight)rows=Math.max(224,(int)Math.ceil(available/scale/8)*8);
        if(canvas!=1)return controller&&!pad&&surfaceWidth>surfaceHeight?Math.min(canvas,rows):canvas;
        return rows;
    }
    void remember() {
        if(sound)requestGameAudio();else releaseGameAudio();
        prefs.edit().putInt("height", canvas).putInt("layers", layers).putFloat("speed", speed)
            .putBoolean("extraSprites",extraSprites).putInt("edgeOffset", edgeOffset).putBoolean("sound", sound).putBoolean("smooth", videoFilter == 1).putInt("videoFilter", videoFilter).putBoolean("integerScale", integerScale).putInt("videoSection", videoSection).putFloat("zoom", zoom).putInt("slot", slot).apply();
    }
    void loadRom(String path, boolean resume) {
        synchronized (engine) {
            if (ready) { state(false, 0); flush(); }
            ready = boot(getFilesDir().getPath(), path, effectiveHeight());
            if (ready) {
                rom = path;
                playedFrames = 0; appliedHeight = -1;
                setLayers(layers);setSprites(extraSprites);
                clearKeys();
                prefs.edit().putString("rom", rom).apply();
                if (resume && saveFile(0).isFile()) state(true, 0);
            }
        }
        wakeLoop();
        if (!ready) toast("游戏数据载入失败，请重新安装此应用");
    }
    void requestGameAudio(){
        if(!background&&windowFocused&&sound&&!audioFocus&&audioManager!=null)
            audioFocus=audioManager.requestAudioFocus(audioRequest)==AudioManager.AUDIOFOCUS_REQUEST_GRANTED;
        wakeLoop();
    }
    void releaseGameAudio(){audioFocus=false;audioEpoch++;if(audioManager!=null&&audioRequest!=null)audioManager.abandonAudioFocusRequest(audioRequest);}
    AudioTrack createAudio(){
        AudioTrack track=null;
        try{
            track=new AudioTrack.Builder()
                .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
                .setAudioFormat(new AudioFormat.Builder().setSampleRate(audioRate()).setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_STEREO).build())
                .setTransferMode(AudioTrack.MODE_STREAM).setBufferSizeInBytes(16384).build();
            if(track.getState()!=AudioTrack.STATE_INITIALIZED){track.release();return null;}
            return track;
        }catch(RuntimeException error){if(track!=null)track.release();android.util.Log.w("QuintetAudio","Audio route not ready; retrying",error);return null;}
    }
    void startLoop() {
        if (running || !ready) return;
        running = true;
        nextVsync=0;
        Choreographer.getInstance().removeFrameCallback(frameClock);
        Choreographer.getInstance().postFrameCallback(frameClock);
        worker = new Thread(() -> {
            try{android.os.Process.setThreadPriority(android.os.Process.THREAD_PRIORITY_DISPLAY);}
            catch(SecurityException denied){android.util.Log.w("TallPerf","Display priority unavailable; using normal priority");}
            short[] pcm = new short[8192];
            AudioTrack audio = null;
            int trackEpoch=-1;
            PcmQueue pending = new PcmQueue(32768);
            int playbackRate=0;
            long lastTick=frameTick, measure=System.nanoTime(), lastFrame=0, worstGap=0;
            int stalled=0;long stepCost=0,drawCost=0;
            int frames = 0;
            VideoEffects effects = new VideoEffects();
            boolean hardwareCanvas = true;
            while (running && worker == Thread.currentThread()) {
                if (menuOpen || background || !ready) {
                    if (audio != null && audio.getPlayState() == AudioTrack.PLAYSTATE_PLAYING) { audio.pause(); audio.flush(); }
                    synchronized(loopWake){try{if(running&&(menuOpen||background||!ready))loopWake.wait();}catch(InterruptedException ignored){}}
                    pending.clear(); lastTick=frameTick; measure=System.nanoTime(); frames=0; lastFrame=0; worstGap=0; stalled=0; stepCost=drawCost=0;
                    continue;
                }
                synchronized(loopWake){
                    while(running&&!menuOpen&&!background&&ready&&frameTick==lastTick)
                        try{loopWake.wait();}catch(InterruptedException ignored){}
                    lastTick=frameTick;
                }
                if(!running||menuOpen||background||!ready)continue;
                // Streaming writes must never wait for the audio device on the
                // emulation/drawing thread. Preserve partially accepted PCM.
                if(audio!=null&&(!audioFocus||trackEpoch!=audioEpoch)){
                    audio.pause();audio.flush();audio.release();audio=null;pending.clear();playbackRate=0;
                }
                if(audio==null&&audioFocus){audio=createAudio();trackEpoch=audioEpoch;pending.clear();playbackRate=0;}
                if(audio!=null){
                    int rate=(int)Math.round(audioRate()*(double)frameNanos()/outputPeriodNanos);
                    try{if(rate!=playbackRate){audio.setPlaybackRate(rate);playbackRate=rate;}}
                    catch(RuntimeException unavailable){audio.release();audio=null;pending.clear();playbackRate=0;android.util.Log.w("QuintetAudio","Audio route changed; rebuilding",unavailable);}
                }
                int n;
                synchronized (engine) {
                    int requested = effectiveHeight();
                    if (requested != appliedHeight) { setCanvas(requested); appliedHeight = requested; }
                    float z=displayScale(surfaceWidth,surfaceHeight,256);
                    configureProfile(getResources().getInteger(R.integer.game_profile),Math.max(256,Math.min(640,(int)Math.ceil(surfaceWidth/z))));
                    long phaseStart=System.nanoTime();
                    n = step(bitmap, pcm, keys, speed);
                    stepCost+=System.nanoTime()-phaseStart;
                    playedFrames += Math.max(1, (int)speed);
                    long drawStart=System.nanoTime();
                    Canvas c = null;
                    try {
                        if(hardwareCanvas){
                            try{c=surface.getHolder().lockHardwareCanvas();}
                            catch(RuntimeException unavailable){hardwareCanvas=false;android.util.Log.w("TallVideo","GPU drawing unavailable; using compatible drawing",unavailable);}
                        }
                        if(c==null)c = surface.getHolder().lockCanvas();
                        if (c != null) {
                            c.drawColor(Color.BLACK);
                            int dims = dimensions(), w = dims >>> 16, h = dims & 65535;
                            // Draw from physical y=0; never reserve status/title/cutout padding.
                            float available = availableHeight(c.getWidth(),c.getHeight());
                            float scale = displayScale(c.getWidth(),c.getHeight(),w);
                            float left = (c.getWidth() - w * scale) / 2;
                            c.save();
                            if (pad) c.clipPath(FlipPadGeometry.visible(c.getWidth(), c.getHeight(), edgeOffset));
                            else c.clipRect(0, 0, c.getWidth(), available);
                            RectF bounds = new RectF(left,0,left+w*scale,h*scale);
                            try { effects.draw(c,bitmap,w,h,bounds,videoFilter); }
                            catch (RuntimeException error) { videoFilter=0;ui.post(()->{remember();toast("滤镜暂时不可用，已恢复原始像素");});effects.draw(c,bitmap,w,h,bounds,0);android.util.Log.e("TallFilter","filter error",error); }
                            c.restore();
                        }
                    } catch (Exception e) { android.util.Log.w("TallVideo", "surface changed", e); }
                    finally { if (c != null) surface.getHolder().unlockCanvasAndPost(c);drawCost+=System.nanoTime()-drawStart; }
                }
                if (audio != null && n > 0) {
                    if(!pending.offer(pcm,n))android.util.Log.w("QuintetAudio","PCM queue overflow; dropping oldest pending audio");
                    int written=pending.drain(audio);
                    if(written<0){android.util.Log.w("QuintetAudio","AudioTrack write failed: "+written+"; rebuilding");audio.release();audio=null;pending.clear();playbackRate=0;}
                    // Queue PCM before starting. PlaybackParams is deliberately
                    // unused: 1x does not need Android's time-stretch pipeline.
                    else if(audio.getPlayState()!=AudioTrack.PLAYSTATE_PLAYING)audio.play();
                }
                frames++;
                long now = System.nanoTime();
                if(lastFrame!=0){long gap=now-lastFrame;worstGap=Math.max(worstGap,gap);if(gap>outputPeriodNanos*1.7)stalled++;}
                lastFrame=now;
                if (now - measure > 3000000000L) {
                    android.util.Log.i("TallPerf", "fps=" + (frames * 1e9 / (now - measure)) + " gameSpeed=" + speed + " pad=" + pad + " dimensions=" + dimensions()
                        + " worstGapMs="+worstGap/1e6+" stalls="+stalled+" pending="+pending.size()
                        + " avgCoreMs="+stepCost/1e6/frames+" avgDrawMs="+drawCost/1e6/frames
                        + " underruns="+(audio==null?-1:audio.getUnderrunCount()));
                    frames=0;measure=now;worstGap=0;stalled=0;stepCost=drawCost=0;
                }
            }
            effects.close();
            if (audio != null) { audio.stop(); audio.release(); }
        }, "TallLoop");
        worker.start();
    }
    void stopLoop() { running = false;Choreographer.getInstance().removeFrameCallback(frameClock);wakeLoop(); if (worker != null) { try { worker.join(1200); } catch (InterruptedException e) { } worker = null; } }
    @Override protected void onResume() { super.onResume(); background = false;requestGameAudio();wakeLoop(); immersive(); if (surface != null && surface.getHolder().getSurface().isValid()) startLoop(); }
    @Override protected void onPause() { background = true;releaseGameAudio();ui.removeCallbacks(selectHold);selectGameHeld=false; clearKeys(); synchronized (engine) { if (ready) { state(false, 0); flush(); } } super.onPause(); }
    @Override protected void onDestroy() { stopLoop(); ((InputManager)getSystemService(INPUT_SERVICE)).unregisterInputDeviceListener(this); synchronized (engine) { flush(); } super.onDestroy(); }
    @Override public void onWindowFocusChanged(boolean focus) { super.onWindowFocusChanged(focus); windowFocused=focus;if (focus) {immersive();requestGameAudio();}else releaseGameAudio(); }

    void toast(String text) {
        ui.post(() -> { notice = text; noticeUntil = System.currentTimeMillis() + 2200; controls.invalidate(); Toast.makeText(this, text, Toast.LENGTH_SHORT).show(); });
    }
    GradientDrawable box(int color) { GradientDrawable d = new GradientDrawable(); d.setColor(color); d.setCornerRadius(dp(10)); return d; }
    void text(String value, int size) { TextView v = new TextView(this); v.setText(value); v.setTextSize(size); v.setTextColor(Color.LTGRAY); v.setPadding(dp(2), dp(6), 0, dp(6)); rows.addView(v); }
    Button button(LinearLayout parent, String label, Runnable action) {
        Button b = new Button(this); b.setText(label); b.setTextSize(16); b.setTextColor(Color.WHITE); b.setAllCaps(false); b.setFocusableInTouchMode(true); b.setBackground(box(CHIP)); b.setMinHeight(dp(48));
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(0, dp(50), 1); lp.setMargins(dp(3), dp(3), dp(3), dp(3)); parent.addView(b, lp);
        int idx = focusButtons.size(); focusButtons.add(b); // Assistant owns selection across touch/controller transitions.
        b.setOnTouchListener((v,e)->{if(e.getActionMasked()==MotionEvent.ACTION_DOWN)v.requestFocus();return false;});
        b.setOnClickListener(v -> { selected = idx; v.performHapticFeedback(HapticFeedbackConstants.CLOCK_TICK); action.run(); }); return b;
    }
    void tintFocus() { for (int i = 0; i < focusButtons.size(); i++) { Button b = focusButtons.get(i); b.setBackground(box(i == selected ? MINT : CHIP)); b.setTextColor(i == selected ? Color.rgb(20, 52, 53) : Color.WHITE); } }
    LinearLayout row() { LinearLayout r = new LinearLayout(this); r.setOrientation(LinearLayout.HORIZONTAL); rows.addView(r); return r; }
    void closeMenu() { if (card != null) root.removeView(card); card = null; menuOpen = false; clearKeys();wakeLoop(); immersive(); }
    void menu() {
        if (menuOpen) { closeMenu(); return; }
        menuOpen = true; clearKeys(); buildMenu();
    }
    File saveFile(int n) { return new File(rom + (n == 0 ? ".suspend.state" : n == 1 ? ".state" : ".slot" + n + ".state")); }
    String stamp(int n) { File f = saveFile(n); return f.isFile() ? new SimpleDateFormat("MM-dd HH:mm", Locale.ROOT).format(new Date(f.lastModified())) : "空"; }
    void buildMenu() {
        boolean samePage = lastBuiltTab == tab && (tab != 2 || lastBuiltSection == videoSection);
        int oldSelected = selected, oldScroll = assistantScroll == null ? 0 : assistantScroll.getScrollY();
        if (card != null) root.removeView(card);
        focusButtons.clear(); selected = 0;
        card = new LinearLayout(this); card.setOrientation(1); card.setPadding(dp(9), dp(10), dp(9), dp(9)); card.setBackground(box(BG));
        int viewHeight=root.getHeight()>0?root.getHeight():surfaceHeight;
        int viewWidth=root.getWidth()>0?root.getWidth():surfaceWidth;
        int bottom=pad?(int)FlipPadGeometry.bottom(viewWidth,viewHeight,edgeOffset):viewHeight-dp(10);
        FrameLayout.LayoutParams place = new FrameLayout.LayoutParams(-1,Math.max(1,bottom-dp(30))); place.setMargins(dp(8), dp(22), dp(8), 0); root.addView(card, place);
        TextView title = new TextView(this); title.setText(getString(R.string.app_name)+" · v"+BuildConfig.VERSION_NAME); title.setTextSize(25); title.setTextColor(Color.WHITE); title.setPadding(dp(4), 0, 0, dp(8)); card.addView(title);
        rows = card;
        LinearLayout tabs = row();
        String[] names = {"存档", "速度", "画面", "辅助"};
        for (int i = 0; i < 4; i++) { final int t = i; Button b = button(tabs, names[i], () -> { tab = t; buildMenu(); }); if (i == tab) b.setTextColor(MINT); }
        if (tab == 2) {
            LinearLayout sections = row();String[] names2={"缩放","滤镜","扩屏"};
            for(int i=0;i<3;i++){final int section=i;Button b=button(sections,names2[i],()->{videoSection=section;remember();buildMenu();});b.setTypeface(null,i==videoSection?Typeface.BOLD:Typeface.NORMAL);}
        }
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(false); assistantScroll=scroll;
        card.addView(scroll, new LinearLayout.LayoutParams(-1,0,1));
        rows = new LinearLayout(this); rows.setOrientation(1); scroll.addView(rows);
        if (tab == 0) {
            text("六个独立手动槽 · 当前 " + slot + " 号槽", 16);
            for (int pair = 0; pair < 3; pair++) {
                LinearLayout group = row();
                for (int col = 0; col < 2; col++) {
                    final int s = pair * 2 + col + 1;
                    LinearLayout cell = new LinearLayout(this); cell.setOrientation(1);
                    LinearLayout.LayoutParams cellp = new LinearLayout.LayoutParams(0, -2, 1); cellp.setMargins(dp(3),dp(2),dp(3),dp(2)); group.addView(cell, cellp);
                    TextView label = new TextView(this); label.setText(s + " 号  " + stamp(s)); label.setTextColor(s == slot ? MINT : Color.LTGRAY); label.setTextSize(14); label.setPadding(dp(5),dp(5),0,0); cell.addView(label);
                    LinearLayout actions = new LinearLayout(this); cell.addView(actions);
                    button(actions, "保存", () -> { synchronized (engine) { slot = s; toast(state(false, s) ? "已保存到 " + s + " 号槽" : "保存失败"); } remember(); buildMenu(); });
                    button(actions, "读取", () -> { boolean ok; synchronized(engine) { ok=state(true,s); } if(ok) {slot=s;remember();closeMenu();toast("已读取 " + s + " 号槽");} else toast("该槽没有可读存档"); });
                }
            }
            text("自动续玩：" + stamp(0), 14);
        } else if (tab == 1) {
            text("游戏速度  " + speed + "×", 18);
            LinearLayout r = row(); for (float v : new float[]{.5f, 1, 2, 3, 4}) { final float rate = v; button(r, (v == (int)v ? ""+(int)v : ""+v)+"×", () -> { speed = rate; remember(); buildMenu(); }); }
            text("L1 减速 · L2 加速\n音乐与音效保持正常播放速率。",15);
            android.media.AudioManager volume=(android.media.AudioManager)getSystemService(AUDIO_SERVICE);
            if(volume!=null&&(volume.getStreamVolume(android.media.AudioManager.STREAM_MUSIC)==0||volume.isStreamMute(android.media.AudioManager.STREAM_MUSIC)))text("手机媒体声音已静音，可用手机音量键恢复。",15);
            r = row(); button(r, sound ? "声音：开" : "声音：关", () -> { sound = !sound; remember(); buildMenu(); });
        } else if (tab == 2) {
            if (videoSection == 0) {
                TextView scaleLabel = new TextView(this);scaleLabel.setTextSize(18);scaleLabel.setTextColor(Color.WHITE);scaleLabel.setPadding(dp(4),dp(7),0,dp(7));rows.addView(scaleLabel);scaleLabel.setText(scaleDescription());
                LinearLayout r=row();button(r,"满宽适配",()->{zoom=0;integerScale=false;remember();buildMenu();});
                button(r,"整数缩放："+(integerScale?"开":"关"),()->{integerScale=!integerScale;if(integerScale&&zoom>0)zoom=Math.max(1,Math.round(zoom));remember();buildMenu();});
                r=row();for(int v=1;v<=6;v++){final int z=v;button(r,v+"×",()->{setZoom(z);buildMenu();});}
                SeekBar seek=new SeekBar(this);seek.setContentDescription("画面缩放滑块");seek.setMax(integerScale?5:100);
                float current=zoom>0?zoom:PortraitLayout.scale(surfaceWidth,Math.max(1,dimensions()>>>16),0,integerScale);
                seek.setProgress(integerScale?Math.round(current)-1:Math.round((current-1)*20));rows.addView(seek,new LinearLayout.LayoutParams(-1,dp(42)));
                seek.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener(){
                    public void onStartTrackingTouch(SeekBar bar){}
                    public void onStopTrackingTouch(SeekBar bar){remember();scaleLabel.setText(scaleDescription());}
                    public void onProgressChanged(SeekBar bar,int value,boolean user){if(user){zoom=integerScale?1+value:1+value*.05f;scaleLabel.setText(scaleDescription());}}
                });
                r=row();button(r,integerScale?"−1×":"−0.25×",()->{adjustZoom(-1);buildMenu();});button(r,"复位",()->{zoom=0;integerScale=true;remember();buildMenu();});button(r,integerScale?"+1×":"+0.25×",()->{adjustZoom(1);buildMenu();});
                text("等比例、顶部对齐。整数缩放使用整倍像素，可能留侧边；超过屏宽的手动放大会居中裁切。",14);
            } else if (videoSection == 1) {
                text("滤镜："+VideoEffects.NAMES[videoFilter],18);
                for(int group=0;group<3;group++){LinearLayout r=row();for(int col=0;col<2;col++){final int effect=group*2+col;button(r,VideoEffects.NAMES[effect]+(videoFilter==effect?" ✓":""),()->{videoFilter=effect;remember();buildMenu();});}}
                text("xBRZ 2× / 3× 先处理像素边缘，再按当前缩放显示。原始像素最流畅，3× 滤镜更耗性能。",14);
            } else {
            text("上下扩屏：" + (canvas == 0 ? "关闭" : canvas == 1 ? "自动" : canvas + " 行") + " · " + (pad ? "FlipPad 布局" : controller ? "手柄全屏" : "触屏宽度适配"), 16);
            LinearLayout r = row(); for (int v : new int[]{1, 0, 320, 416, 480}) { final int h = v; button(r, v == 0 ? "原始" : v == 1 ? "自动" : ""+v, () -> { canvas = h; synchronized (engine) { setCanvas(effectiveHeight()); } remember(); buildMenu(); }); }
            text("FlipPad 底边位置", 14);
            r = row(); button(r, "上移 10", () -> { edgeOffset = Math.max(-120, edgeOffset - 10); remember(); buildMenu(); });
            button(r, "复位", () -> { edgeOffset = 0; remember(); buildMenu(); });
            button(r, "下移 10", () -> { edgeOffset = Math.min(120, edgeOffset + 10); remember(); buildMenu(); });
            text("扩展背景", 16); r = row(); String[] ls = {"全部背景", "背景二", "背景一"};
            for (int i = 0; i < 3; i++) { final int l = i; button(r, ls[i] + (layers == i ? " ✓" : ""), () -> { layers = l; synchronized(engine) { setLayers(l); } remember(); buildMenu(); }); }
                button(row(),"屏外人物与物件："+(extraSprites?"开":"关"),()->{extraSprites=!extraSprites;synchronized(engine){setSprites(extraSprites);}remember();buildMenu();});
                text("扩展区读取游戏的完整地图；房间边界外保留黑色。特殊画面自动保留原始画面。",14);
            }
        } else {
            LinearLayout r=row();button(r,"物品 / 装备",()->pulse(getResources().getInteger(R.integer.game_profile)==1?1<<9:1<<2));
            r=row();button(r,"原版 L",()->pulse(1<<10));button(r,"原版 R",()->pulse(1<<11));
            r=row();button(r,"原版 Select",()->pulse(1<<2));button(r,"原版 Start",()->pulse(1<<3));
            button(row(),"重新开始",()->new AlertDialog.Builder(this).setMessage("返回游戏标题？六个手动槽会保留。").setPositiveButton("返回标题",(d,w)->{synchronized(engine){resetCore();}closeMenu();}).setNegativeButton("取消",null).show());
            text("Select：游戏助手\nStart：原版 Start\nL1 / L2：减速 / 加速\nR1：视野 · R2：滤镜\n插拔手柄自动切换触屏按键。",14);

        }
        rows = card; text("方向键选择 · A 确认 · B / Select 返回 · L/R 切换分类", 14);
        button(row(), "继续游戏", this::closeMenu);
        selected=samePage?Math.max(0,Math.min(oldSelected,focusButtons.size()-1)):Math.min(tab,focusButtons.size()-1);
        lastBuiltTab=tab;lastBuiltSection=videoSection;tintFocus();
        if(!focusButtons.isEmpty())focusButtons.get(selected).requestFocus();
        if(samePage)scroll.post(()->scroll.scrollTo(0,oldScroll));
    }
    String scaleDescription(){int w=dimensions()>>>16;float actual=displayScale(surfaceWidth,surfaceHeight,w);return "画面缩放："+(zoom==0?(integerScale?"自动整数适配":"满宽适配"):String.format(Locale.ROOT,"%.2f×",zoom))+String.format(Locale.ROOT,"\n实际 %.2f×",actual);}
    void setZoom(float value){zoom=Math.max(1,Math.min(6,value));if(integerScale)zoom=Math.round(zoom);remember();}
    void adjustZoom(int direction){float current=zoom>0?zoom:PortraitLayout.scale(surfaceWidth,Math.max(1,dimensions()>>>16),0,integerScale);setZoom(current+direction*(integerScale?1:.25f));}
    void changeSpeed(int delta) { float[] a = {.5f, 1, 2, 3, 4}; int i = 0; while (i < 4 && a[i] < speed) i++; speed = a[Math.max(0, Math.min(4, i + delta))]; remember(); toast("游戏速度 " + speed + "×"); }
    int map(int key) { switch(key) { case 19:return 4; case 20:return 5; case 21:return 6; case 22:return 7; case 96:return 8; case 97:return 0; case 99:return 9; case 100:return 1;  case 106:case 107:return 2; case 108:return 3; default:return -1; } }
    @Override public boolean dispatchKeyEvent(KeyEvent e) {
        int k = e.getKeyCode(); boolean down = e.getAction() == KeyEvent.ACTION_DOWN;
        if (menuOpen) {
            if (k == 4 || k == 97 || k == 109) { if (!down) closeMenu(); return true; }
            if (k == 102 || k == 103) { if (down && e.getRepeatCount() == 0) { tab = (tab + (k == 102 ? 3 : 1)) % 4; buildMenu(); } return true; }
            if (k == 19 || k == 21 || k == 20 || k == 22) { if (down) { selected = (selected + ((k == 19 || k == 21) ? focusButtons.size()-1 : 1)) % focusButtons.size(); tintFocus(); focusButtons.get(selected).requestFocus(); focusButtons.get(selected).getParent().requestChildFocus(focusButtons.get(selected), focusButtons.get(selected)); focusButtons.get(selected).requestRectangleOnScreen(new Rect(0,0,focusButtons.get(selected).getWidth(),focusButtons.get(selected).getHeight())); } return true; }
            if (k == 96 || k == 23 || k == 66) { if (!down && !focusButtons.isEmpty()) focusButtons.get(selected).performClick(); return true; }
            return super.dispatchKeyEvent(e);
        }
        if(k==109){if(down&&e.getRepeatCount()==0){selectGameHeld=false;ui.postDelayed(selectHold,550);}else if(!down){ui.removeCallbacks(selectHold);if(selectGameHeld){keys&=~(1<<2);selectGameHeld=false;}else menu();}return true;}
        if (k == 4) { if (!down) menu(); return true; }
        if(k>=102&&k<=105){if(down&&e.getRepeatCount()==0){
            if(k==102||k==104)changeSpeed(k==102?-1:1);
            if(k==103){float[] zs={0,3,4,5};int at=0;for(int i=0;i<zs.length;i++)if(zoom==zs[i])at=i;zoom=zs[(at+1)%zs.length];remember();toast(zoom==0?"整数适配":"视野 "+zoom+"×");}
            if(k==105){videoFilter=(videoFilter+1)%VideoEffects.NAMES.length;remember();toast(VideoEffects.NAMES[videoFilter]);}
        }return true;}
        int id = map(k); if (id < 0) return super.dispatchKeyEvent(e);
        if(id>=4&&id<=7){
            if(down)keyDirections|=1<<id;else keyDirections&=~(1<<id);
            keys=(keys&~240)|keyDirections|axisDirections;
        }else if (down) keys |= 1 << id; else keys &= ~(1 << id);
        if (e.getRepeatCount() == 0) android.util.Log.i("TallInput", "key="+k+" action="+e.getAction()+" device="+e.getDeviceId());
        return true;
    }
    @Override public boolean onGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) != 0 && e.getAction() == MotionEvent.ACTION_MOVE) {
            float x = e.getAxisValue(MotionEvent.AXIS_HAT_X), y = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
            if (x == 0) x = e.getAxisValue(MotionEvent.AXIS_X); if (y == 0) y = e.getAxisValue(MotionEvent.AXIS_Y);
            if (menuOpen) {
                int direction = y < -.5f || x < -.5f ? -1 : y > .5f || x > .5f ? 1 : 0;
                long now = System.currentTimeMillis();
                if (direction != 0 && (direction != lastHat || now - lastHatTime > 180) && !focusButtons.isEmpty()) {
                    selected = (selected + (direction < 0 ? focusButtons.size() - 1 : 1)) % focusButtons.size();
                    tintFocus(); Button chosen = focusButtons.get(selected); chosen.requestFocus(); chosen.requestRectangleOnScreen(new Rect(0,0,chosen.getWidth(),chosen.getHeight())); lastHatTime = now;
                }
                lastHat = direction; return true;
            }
            int b=0; if(x<-.5)b|=64;if(x>.5)b|=128;if(y<-.5)b|=16;if(y>.5)b|=32;axisDirections=b;keys=(keys&~240)|keyDirections|axisDirections;return true;
        }
        return super.onGenericMotionEvent(e);
    }
    static void copyBounded(InputStream in, OutputStream out, int limit) throws IOException {
        byte[] b = new byte[65536]; int n, total = 0; while ((n = in.read(b)) != -1) { total += n; if (total > limit) throw new IOException("ROM 大于 32 MB"); out.write(b, 0, n); }
    }
    class Controls extends View {
        final Paint p=new Paint(Paint.ANTI_ALIAS_FLAG);
        final ArrayList<RectF> rects=new ArrayList<>();
        final int[] masks={16,32,64,128,1<<1,1<<0,1<<8,1<<10,1<<9,1<<11,1<<3,0};
        final String[] labels={"↑","↓","←","→","Y","B","A","L","X","R","START","助手"};
        Controls(){super(MainActivity.this);setFocusable(false);}
        void layoutKeys(){
            rects.clear();
            float[][] bounds = PortraitLayout.buttons(getWidth(), getHeight(), getResources().getDisplayMetrics().density);
            for (float[] r : bounds) rects.add(new RectF(r[0], r[1], r[2], r[3]));
        }
        protected void onDraw(Canvas c){super.onDraw(c);if(controller)return;layoutKeys();p.setTextAlign(Paint.Align.CENTER);p.setTextSize(dp(18));for(int i=0;i<rects.size();i++){RectF r=rects.get(i);p.setColor((keys&masks[i])!=0?MINT:CHIP);c.drawRoundRect(r,dp(10),dp(10),p);p.setColor(Color.WHITE);c.drawText(labels[i],r.centerX(),r.centerY()+dp(6),p);}}
        public boolean onTouchEvent(MotionEvent e){if(menuOpen)return false;if(e.getActionMasked()==MotionEvent.ACTION_CANCEL){clearKeys();invalidate();return true;}if(controller){if(e.getAction()==MotionEvent.ACTION_UP)menu();return true;}layoutKeys();int active=0;boolean hit=false;for(int k=0;k<e.getPointerCount();k++){if((e.getActionMasked()==MotionEvent.ACTION_UP||e.getActionMasked()==MotionEvent.ACTION_POINTER_UP)&&e.getActionIndex()==k)continue;float x=e.getX(k),y=e.getY(k);for(int i=0;i<rects.size();i++)if(rects.get(i).contains(x,y)){active|=masks[i];hit=true;}}
            if(e.getActionMasked()==MotionEvent.ACTION_UP){float x=e.getX(),y=e.getY();if(rects.get(11).contains(x,y)||y<PortraitLayout.touchTop(getHeight(),getResources().getDisplayMetrics().density))menu();}
            keys=active;invalidate();return true;}
    }
}
