// CipherSNES portable X11 frontend (CP56 build fallback).
// Created as a buildable replacement on hosts lacking GTK3/SDL2 development headers.
// The emulation core and original GTK3 frontend remain untouched.
#include <CipherSNES/Core.hpp>
#include <CipherSNES/FramePacer.hpp>
#include <CipherSNES/NearestScaler.hpp>
#include <CipherSNES/PresentationRegion.hpp>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cctype>
#include <stdexcept>
#include <poll.h>
#include <cerrno>
#include <ctime>
#include <string>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

namespace {
using namespace snes;
using Clock = std::chrono::steady_clock;
std::int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }

constexpr std::array<KeySym, buttonCount> kKeys{XK_z,XK_a,XK_Shift_R,XK_Return,XK_Up,XK_Down,XK_Left,XK_Right,XK_x,XK_s,XK_q,XK_w};
constexpr std::array<int,buttonCount> kPad{0,2,4,6,11,12,13,14,1,3,9,10};
// SDL2 runtime ABI subset. Development headers are intentionally not required.
using U8=std::uint8_t; using U16=std::uint16_t; using U32=std::uint32_t;
struct AudioSpec {int freq;U16 format;U8 channels;U8 silence;U16 samples;U16 padding;U32 size;void(*callback)(void*,U8*,int);void* userdata;};
struct Sdl {
    // SDL2 is dynamically loaded: no SDL2 development headers required on the
    // low-end target, and the audio and gamepad subsystems are independent.
    void* lib=nullptr; bool padOk=false,audioOk=false; unsigned device=0;
    void* controller=nullptr;void* joystick=nullptr;int connectedIndex=-1;
    std::string padName="No gamepad detected",status;
    U32 initialized=0;
    int (*init)(U32)=nullptr; void (*quit)(U32)=nullptr;
    void (*pump)()=nullptr;void (*joystickUpdate)()=nullptr;
    const char* (*getError)()=nullptr;
    unsigned(*openAudio)(const char*,int,const AudioSpec*,AudioSpec*,int)=nullptr;
    void(*pauseAudio)(unsigned,int)=nullptr; void(*closeAudio)(unsigned)=nullptr;
    int(*queueAudio)(unsigned,const void*,U32)=nullptr;U32(*queuedSize)(unsigned)=nullptr;
    void(*clearAudio)(unsigned)=nullptr;
    int(*numJoysticks)()=nullptr;int(*isController)(int)=nullptr;
    void*(*controllerOpen)(int)=nullptr;void(*controllerClose)(void*)=nullptr;
    int(*controllerAttached)(void*)=nullptr;U8(*controllerButton)(void*,int)=nullptr;
    std::int16_t(*controllerAxis)(void*,int)=nullptr;
    const char*(*controllerName)(void*)=nullptr;
    void*(*joystickOpen)(int)=nullptr;void(*joystickClose)(void*)=nullptr;
    int(*joystickAttached)(void*)=nullptr;U8(*joystickButton)(void*,int)=nullptr;
    U8(*joystickHat)(void*,int)=nullptr;std::int16_t(*joystickAxis)(void*,int)=nullptr;
    int(*joystickNumButtons)(void*)=nullptr;const char*(*joystickName)(void*)=nullptr;
    template<class T> bool load(T& fn,const char* name){fn=reinterpret_cast<T>(dlsym(lib,name));return fn!=nullptr;}
    Sdl(){
        lib=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
        if(!lib){status="SDL2 runtime missing: install libsdl2-2.0-0";return;}
        if(!load(init,"SDL_InitSubSystem")||!load(quit,"SDL_QuitSubSystem")||
           !load(pump,"SDL_PumpEvents")||!load(joystickUpdate,"SDL_JoystickUpdate")||
           !load(numJoysticks,"SDL_NumJoysticks")||!load(isController,"SDL_IsGameController")||
           !load(controllerOpen,"SDL_GameControllerOpen")||!load(controllerClose,"SDL_GameControllerClose")||
           !load(controllerAttached,"SDL_GameControllerGetAttached")||
           !load(controllerButton,"SDL_GameControllerGetButton")||
           !load(controllerAxis,"SDL_GameControllerGetAxis")||
           !load(joystickOpen,"SDL_JoystickOpen")||!load(joystickClose,"SDL_JoystickClose")||
           !load(joystickAttached,"SDL_JoystickGetAttached")||
           !load(joystickButton,"SDL_JoystickGetButton")||
           !load(joystickHat,"SDL_JoystickGetHat")||!load(joystickAxis,"SDL_JoystickGetAxis")||
           !load(joystickNumButtons,"SDL_JoystickNumButtons")){
            status="SDL2 missing required controller functions";return;
        }
        load(controllerName,"SDL_GameControllerName");
        load(joystickName,"SDL_JoystickName");
        load(getError,"SDL_GetError");
        // Enable Joystick + GameController independently of any audio device.
        if(init(0x200u|0x2000u)==0){padOk=true;initialized|=0x200u|0x2000u;}
        else status=std::string("SDL controller init: ")+(getError?getError():"unknown error");
        if(load(openAudio,"SDL_OpenAudioDevice")&&load(pauseAudio,"SDL_PauseAudioDevice")&&
           load(closeAudio,"SDL_CloseAudioDevice")&&load(queueAudio,"SDL_QueueAudio")&&
           load(queuedSize,"SDL_GetQueuedAudioSize")&&load(clearAudio,"SDL_ClearQueuedAudio")&&
           init(0x10u)==0){
            audioOk=true;initialized|=0x10u;
            AudioSpec spec{};spec.freq=32000;spec.format=0x8010;spec.channels=2;spec.samples=1024;
            device=openAudio(nullptr,0,&spec,nullptr,0);
            if(device)pauseAudio(device,0);
        }
        scan(true);
    }
    void closePad(){
        if(controller){controllerClose(controller);controller=nullptr;}
        if(joystick){joystickClose(joystick);joystick=nullptr;}
        connectedIndex=-1;padName="No gamepad detected";
    }
    bool attached()const{return controller?controllerAttached(controller)!=0:
        joystick?joystickAttached(joystick)!=0:false;}
    void scan(bool force=false){
        if(!padOk)return;
        pump();joystickUpdate();
        if(attached()&&!force)return;
        closePad();
        int count=numJoysticks();
        // SDL_GameController first (correct standardized buttons/D-pad), then
        // raw SDL_Joystick fallback for generic USB and legacy controllers.
        for(int i=0;i<count&&!controller;++i){
            if(isController(i)){controller=controllerOpen(i);if(controller)connectedIndex=i;}
        }
        if(!controller)for(int i=0;i<count&&!joystick;++i){
            joystick=joystickOpen(i);if(joystick)connectedIndex=i;
        }
        if(controller){const char* n=controllerName?controllerName(controller):nullptr;
            padName=std::string(n&&*n?n:"SDL game controller")+" (mapped)";}
        else if(joystick){const char* n=joystickName?joystickName(joystick):nullptr;
            padName=std::string(n&&*n?n:"Generic joystick")+" (raw)";}
    }
    void tick(){if(!padOk)return;pump();joystickUpdate();if((controller||joystick)&&!attached())closePad();}
    bool available()const{return attached();}
    int buttons()const{return controller?15:joystick?std::max(0,joystickNumButtons(joystick)):0;}
    bool button(int index)const{
        if(index<0||index>=buttons())return false;
        return controller?controllerButton(controller,index)!=0:joystickButton(joystick,index)!=0;
    }
    bool direction(int snesIndex)const{
        if(!available()||snesIndex<4||snesIndex>7)return false;
        // Standard controller D-pad is addressed via button bindings.
        // Raw joysticks commonly expose D-pad as hat 0; support both.
        if(joystick){U8 hat=joystickHat(joystick,0);
            if(hat&(snesIndex==4?1:snesIndex==5?4:snesIndex==6?8:2))return true;
        }
        const int axis=(snesIndex<=5)?1:0;
        const int raw=controller?controllerAxis(controller,axis):joystickAxis(joystick,axis);
        return (snesIndex==4||snesIndex==6)?raw<-16000:raw>16000;
    }
    ~Sdl(){closePad();if(device)closeAudio(device);if(initialized)quit(initialized);if(lib)dlclose(lib);}
};
struct App {
    Display* display=nullptr; Window window=0; GC gc{}; Atom wmDelete{};
    // CP58: all rendering is composed into an offscreen server pixmap and
    // copied to the window with ONE X request. Drawing directly to a visible
    // window caused the black full-window clear to flash between video frames.
    Pixmap backbuffer=0;
    int backWidth=0,backHeight=0;
    VideoRegion previousVideoRegion{};
    bool previousVideoValid=false;
    unsigned long fullCopies=0, videoOnlyCopies=0;
    Drawable paintTarget() const { return backbuffer ? backbuffer : window; }
    void ensureBackbuffer(){
        if(backbuffer && backWidth==width && backHeight==height)return;
        const int w=std::max(1,width),h=std::max(1,height);
        const Pixmap fresh=XCreatePixmap(display,window,static_cast<unsigned>(w),
            static_cast<unsigned>(h),static_cast<unsigned>(DefaultDepth(display,DefaultScreen(display))));
        if(!fresh)throw std::runtime_error("Cannot allocate double-buffered X11 render surface");
        if(backbuffer)XFreePixmap(display,backbuffer);
        backbuffer=fresh;backWidth=width;backHeight=height;
        previousVideoValid=false;
    }
    XFontStruct* font=nullptr; XFontStruct* fontBold=nullptr;
    int width=1024,height=768,uiPercent=125,menuHeight=68,rowHeight=52;
    bool running=true,loaded=false,paused=false,needsRedraw=true,hasFocus=true,mapped=true,fullscreen=false,audioEnabled=true;
    enum class Dialog {Closed,Browser,Controller,About};
    Dialog dialog=Dialog::Closed;
    int activeMenu=-1,hoverItem=-1,scrollOffset=0,selectedEntry=-1,captureButton=-1;
    bool editingPath=false;
    std::array<KeySym,buttonCount> keys=kKeys;
    std::array<bool,buttonCount> held{};
    std::array<int,buttonCount> padMapping{{0,2,4,6,11,12,13,14,1,3,9,10}};
    bool padMappingMode=false;std::array<bool,64> priorPadButtons{};
    std::int64_t lastPadScan=0;std::string shownPadName;
    pid_t chooserPid=-1;int chooserFd=-1;std::string chooserData;

    Core core;FramePacer pacer;Sdl sdl;
    std::string title="CipherSNES",error,pathText;
    std::filesystem::path browseDir;
    struct Entry { std::string label; std::filesystem::path path; bool directory; };
    std::vector<Entry> entries;
    std::vector<std::uint32_t> scaled; XImage* image=nullptr;
    std::vector<std::uint8_t> aboutIconRgb; int aboutIconW=0, aboutIconH=0;
    std::vector<std::int16_t> sound;
    std::int64_t sampleStart=0;unsigned sampleFrames=0;
    std::int64_t lastClickNs=0;int lastClickEntry=-1;
    std::filesystem::path configPath;
    std::string buttonName[buttonCount]={"B","Y","Select","Start","Up","Down","Left","Right","A","X","L","R"};

    App(){
        display=XOpenDisplay(nullptr);
        if(!display)throw std::runtime_error("No X11 display. Start an X11 desktop/XWayland session.");
        int screen=DefaultScreen(display);
        if(DefaultDepth(display,screen)<24)throw std::runtime_error("A 24/32-bit X11 display is required");
        if(const char* home=std::getenv("HOME")){
            configPath=std::filesystem::path(home)/".config/HodgkinsStudios/CipherSNES/keyboard.cfg";
            browseDir=std::filesystem::path(home)/"Downloads";
            if(!std::filesystem::is_directory(browseDir))browseDir=home;
        } else browseDir=".";
        loadSettings();
        loadAboutIcon();
        resizeFonts();
        window=XCreateSimpleWindow(display,RootWindow(display,screen),100,65,width,height,0,
            BlackPixel(display,screen),BlackPixel(display,screen));
        XSizeHints hints{};hints.flags=PMinSize;hints.min_width=720;hints.min_height=560;
        XSetWMNormalHints(display,window,&hints);
        // Do not let the X server erase the displayed frame automatically on
        // Expose/resize; our complete pixmap composition replaces it atomically.
        XSetWindowBackgroundPixmap(display,window,None);
        XSelectInput(display,window,ExposureMask|StructureNotifyMask|PropertyChangeMask|KeyPressMask|KeyReleaseMask|
                     ButtonPressMask|ButtonReleaseMask|PointerMotionMask|FocusChangeMask);
        wmDelete=XInternAtom(display,"WM_DELETE_WINDOW",False);
        XSetWMProtocols(display,window,&wmDelete,1);
        gc=XCreateGC(display,window,0,nullptr);
        XSetGraphicsExposures(display,gc,False);
        XSetFont(display,gc,font->fid);
        XStoreName(display,window,"CipherSNES - Load a SNES ROM");
        installWindowIcon();
        XMapWindow(display,window);
        shownPadName=sdl.padName;
    }
    ~App(){
        if(chooserPid>0){kill(chooserPid,SIGTERM);waitpid(chooserPid,nullptr,0);}
        if(chooserFd>=0)close(chooserFd);
        if(loaded){std::string e;if(!core.save(e))std::cerr<<"SRAM save failed: "<<e<<'\n';}
        freeImage();
        if(display){
            if(backbuffer)XFreePixmap(display,backbuffer);
            if(font)XFreeFont(display,font);
            if(fontBold)XFreeFont(display,fontBold);
            if(gc)XFreeGC(display,gc);
            if(window)XDestroyWindow(display,window);
            XCloseDisplay(display);
        }
    }
    void freeImage(){if(image){image->data=nullptr;XDestroyImage(image);image=nullptr;}}
    // PNG is the distribution master. A small portable PPM derivative is used
    // in this X11-only frontend so we do not require GTK or image libraries.
    void loadAboutIcon(){
        std::vector<std::filesystem::path> candidates;
        std::error_code ec;
        const auto executable=std::filesystem::read_symlink("/proc/self/exe",ec);
        if(!ec) {
            candidates.push_back(executable.parent_path().parent_path()/"assets/CipherSNES_about.ppm");
            candidates.push_back(executable.parent_path()/"../share/ciphersnes/CipherSNES_about.ppm");
        }
        candidates.push_back("assets/CipherSNES_about.ppm");
        for(const auto& candidate:candidates){
            std::ifstream in(candidate,std::ios::binary);
            if(!in)continue;
            std::string magic;int w=0,h=0,max=0;
            if(!(in>>magic>>w>>h>>max) || magic!="P6" || w<1 || h<1 || w>512 || h>512 || max!=255)continue;
            if(in.get()!='\n')continue;
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(w)*h*3u);
            if(!in.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size())))continue;
            aboutIconW=w;aboutIconH=h;aboutIconRgb=std::move(bytes);return;
        }
        std::cerr<<"CipherSNES: optional About icon asset not found\n";
    }
    static unsigned long pixelChannel(std::uint8_t component,unsigned long mask){
        if(!mask)return 0;
        unsigned shift=0;while(((mask>>shift)&1ul)==0ul && shift+1<sizeof(unsigned long)*8)++shift;
        const unsigned long range=mask>>shift;
        return ((static_cast<unsigned long>(component)*range+127)/255<<shift)&mask;
    }
    void drawAboutIcon(int x,int y){
        if(aboutIconRgb.empty())return;
        const int screen=DefaultScreen(display);
        auto* visual=DefaultVisual(display,screen);
        auto* icon=XCreateImage(display,visual,static_cast<unsigned>(DefaultDepth(display,screen)),
            ZPixmap,0,nullptr,aboutIconW,aboutIconH,32,0);
        if(!icon)return;
        icon->data=static_cast<char*>(std::malloc(static_cast<std::size_t>(icon->bytes_per_line)*aboutIconH));
        if(!icon->data){icon->data=nullptr;XDestroyImage(icon);return;}
        for(int iy=0;iy<aboutIconH;++iy)for(int ix=0;ix<aboutIconW;++ix){
            const auto p=(static_cast<std::size_t>(iy)*aboutIconW+ix)*3u;
            const unsigned long rgb=pixelChannel(aboutIconRgb[p],visual->red_mask)|
                pixelChannel(aboutIconRgb[p+1],visual->green_mask)|
                pixelChannel(aboutIconRgb[p+2],visual->blue_mask);
            XPutPixel(icon,ix,iy,rgb);
        }
        XPutImage(display,paintTarget(),gc,icon,0,0,x,y,
            static_cast<unsigned>(aboutIconW),static_cast<unsigned>(aboutIconH));
        XDestroyImage(icon);
    }
    void installWindowIcon(){
        if(aboutIconRgb.empty())return;
        constexpr int size=48;
        std::vector<unsigned long> pixels(2+size*size);
        pixels[0]=size;pixels[1]=size;
        for(int iy=0;iy<size;++iy)for(int ix=0;ix<size;++ix){
            const auto p=(static_cast<std::size_t>(iy*aboutIconH/size)*aboutIconW+
                (ix*aboutIconW/size))*3u;
            pixels[2+iy*size+ix]=0xff000000ul|
                (static_cast<unsigned long>(aboutIconRgb[p])<<16)|
                (static_cast<unsigned long>(aboutIconRgb[p+1])<<8)|aboutIconRgb[p+2];
        }
        const Atom atom=XInternAtom(display,"_NET_WM_ICON",False);
        XChangeProperty(display,window,atom,XA_CARDINAL,32,PropModeReplace,
            reinterpret_cast<const unsigned char*>(pixels.data()),static_cast<int>(pixels.size()));
    }
    void resizeFonts(){
        int px=std::clamp(20*uiPercent/100,20,36);
        auto load=[&](bool bold)->XFontStruct*{
            std::string pattern="-*-helvetica-"+std::string(bold?"bold":"medium")+"-r-normal--"+std::to_string(px)+"-*-*-*-*-*-iso8859-1";
            XFontStruct* f=XLoadQueryFont(display,pattern.c_str());
            if(!f)f=XLoadQueryFont(display,"-*-helvetica-medium-r-normal--20-*-*-*-*-*-iso8859-1");
            if(!f)f=XLoadQueryFont(display,"fixed");
            return f;
        };
        XFontStruct* f=load(false);XFontStruct* b=load(true);
        if(!f||!b)throw std::runtime_error("No X11 core font is available");
        if(font)XFreeFont(display,font);
        if(fontBold)XFreeFont(display,fontBold);
        font=f;fontBold=b;
        menuHeight=std::max(56,54*uiPercent/100);
        rowHeight=std::max(46,43*uiPercent/100);
        if(gc)XSetFont(display,gc,font->fid);
        needsRedraw=true;
    }
    int unit(int value)const{return std::max(1,value*uiPercent/100);}
    int textWidth(const std::string& s,bool bold=false)const{
        auto* f=bold?fontBold:font;
        return XTextWidth(f,s.c_str(),static_cast<int>(s.size()));
    }
    void fill(int x,int y,int w,int h,unsigned long color){
        if(w<=0||h<=0)return;
        XSetForeground(display,gc,color);
        XFillRectangle(display,paintTarget(),gc,x,y,static_cast<unsigned>(w),static_cast<unsigned>(h));
    }
    void frame(int x,int y,int w,int h,unsigned long color){
        XSetForeground(display,gc,color);
        XDrawRectangle(display,paintTarget(),gc,x,y,static_cast<unsigned>(std::max(0,w-1)),static_cast<unsigned>(std::max(0,h-1)));
    }
    void drawText(int x,int baseline,const std::string& value,unsigned long color=0xe8eaf0,bool bold=false,int available=-1){
        if(available==0)return;
        std::string text=value;
        if(available>0&&textWidth(text,bold)>available){
            while(!text.empty()&&textWidth(text+"...",bold)>available)text.pop_back();
            text+="...";
        }
        XSetFont(display,gc,(bold?fontBold:font)->fid);
        XSetForeground(display,gc,color);
        XDrawString(display,paintTarget(),gc,x,baseline,text.c_str(),static_cast<int>(text.size()));
    }
    int midBaseline(int top,int h)const{return top+(h+font->ascent-font->descent)/2;}
    std::array<std::string,4> headers{{"File","Config","View","Help"}};
    struct Rect {int x,y,w,h;bool contains(int px,int py)const{return px>=x&&px<x+w&&py>=y&&py<y+h;}};
    Rect menuHeader(int idx)const{
        int x=unit(14);
        for(int i=0;i<idx;i++)x+=textWidth(headers[i],true)+unit(38);
        return {x,0,textWidth(headers[idx],true)+unit(32),menuHeight};
    }
    std::vector<std::string> menuLabels(int idx)const{
        switch(idx){
        case 0:return {"Load SNES ROM...     Ctrl+O","Reset console           F5","Exit CipherSNES   Ctrl+Q"};
        case 1:return {"Controller settings...","Rescan gamepads","Audio: "+std::string(audioEnabled?"On":"Muted")};
        case 2:return {"Fullscreen                 F11"};
        default:return {"About / Controls..."};
        }
    }
    Rect dropdown(int idx)const{
        const auto labels=menuLabels(idx);int w=unit(310);
        for(auto& s:labels)w=std::max(w,textWidth(s)+unit(44));
        const int maxW=std::max(100,width-unit(24));w=std::min(w,maxW);
        int x=menuHeader(idx).x;x=std::min(x,width-w-unit(10));
        return {x,menuHeight,w,static_cast<int>(labels.size())*rowHeight+unit(12)};
    }
    void drawBar(){
        fill(0,0,width,menuHeight,0x252c3b);
        fill(0,menuHeight-2,width,2,0x4a5a75);
        for(int i=0;i<4;i++){
            Rect r=menuHeader(i);
            if(i==activeMenu)fill(r.x,r.y+unit(4),r.w,menuHeight-unit(8),0x4264a0);
            drawText(r.x+unit(16),midBaseline(0,menuHeight),headers[i],0xf9fbff,true);
        }
        const std::string status=loaded?(paused?"PAUSED":"RUNNING"):"NO ROM";
        const int right=unit(16);
        const int end=width-right;
        const int start=end-textWidth(status,true)-unit(18);
        if(start>menuHeader(3).x+menuHeader(3).w+unit(25))
            drawText(start,midBaseline(0,menuHeight),status,loaded?0x98e0ae:0xc2cbdc,true);
    }
    void drawDropdown(){
        if(activeMenu<0)return;
        Rect r=dropdown(activeMenu);const auto labels=menuLabels(activeMenu);
        fill(r.x,r.y,r.w,r.h,0x202735);frame(r.x,r.y,r.w,r.h,0x7995b8);
        for(int i=0;i<static_cast<int>(labels.size());i++){
            int rowY=r.y+unit(6)+i*rowHeight;
            if(i==hoverItem)fill(r.x+unit(5),rowY,r.w-unit(10),rowHeight,0x3d5680);
            drawText(r.x+unit(18),midBaseline(rowY,rowHeight),labels[i],0xf7f8fa,false,r.w-unit(35));
        }
    }
    void loadSettings(){
        std::ifstream in(configPath);std::string line;
        if(!in)return;
        if(std::getline(in,line))try{
            const int v=std::stoi(line);if(v==100||v==125||v==150)uiPercent=v;
        }catch(...){}
        if(std::getline(in,line))audioEnabled=(line!="0");
        for(unsigned i=0;i<buttonCount&&std::getline(in,line);i++)try{
            const auto n=std::stoul(line);if(n>0&&n<0x1000000u)keys[i]=static_cast<KeySym>(n);
        }catch(...){}
        for(unsigned i=0;i<buttonCount&&std::getline(in,line);i++)try{
            const int n=std::stoi(line);if(n>=0&&n<64)padMapping[i]=n;
        }catch(...){}
    }
    void saveSettings(){
        try{
            std::filesystem::create_directories(configPath.parent_path());
            std::ofstream out(configPath,std::ios::trunc);
            out<<uiPercent<<'\n'<<(audioEnabled?1:0)<<'\n';
            for(auto k:keys)out<<static_cast<unsigned long>(k)<<'\n';
            for(auto b:padMapping)out<<b<<'\n';
        }catch(const std::exception& e){error=std::string("Settings: ")+e.what();}
    }
    void openRom(const std::string& candidate){
        if(candidate.empty())return;
        std::string e;
        std::filesystem::path base;
        if(const char* xdg=std::getenv("XDG_DATA_HOME"))base=xdg;
        if(base.empty())if(const char* home=std::getenv("HOME"))base=std::filesystem::path(home)/".local/share";
        if(base.empty())base=".";
        base/="HodgkinsStudios/CipherSNES";
        // Do not discard the running game if the requested file cannot be opened.
        if(loaded){std::string saveErr;core.save(saveErr);}
        if(!core.loadRom(candidate,(base/"saves").string(),e)){
            error="Load failed: "+e;std::cerr<<error<<'\n';needsRedraw=true;return;
        }
        std::filesystem::path ipl;
        if(const char* env=std::getenv("CIPHERSNES_IPL_PATH"))ipl=env;
        else ipl=base/"firmware"/"spc700.ipl";
        if(std::filesystem::is_regular_file(ipl)&&!core.loadIplFile(ipl.string(),e))
            std::cerr<<"IPL warning: "<<e<<'\n';
        loaded=true;paused=false;error.clear();held.fill(false);
        dialog=Dialog::Closed;activeMenu=-1;
        title="CipherSNES - "+core.cartridgeInfo().title;
        pacer.configure(core.isPal(),nowNs());sampleStart=0;sampleFrames=0;
        if(sdl.device)sdl.clearAudio(sdl.device);
        XStoreName(display,window,title.c_str());needsRedraw=true;
    }
    void reset(){if(!loaded)return;std::string e;if(!core.reset(e))error=e;
        if(sdl.device)sdl.clearAudio(sdl.device);
        pacer.configure(core.isPal(),nowNs());
        needsRedraw=true;
    }
    Rect dialogRect()const{const int w=std::min(width-unit(32),unit(770));
        const int h=std::min(height-menuHeight-unit(32),unit(575));
        return {(width-w)/2,menuHeight+std::max(8,(height-menuHeight-h)/2),w,h};}
    Rect buttonRect(int index,int count,Rect r)const{
        const int gap=unit(10),pad=unit(22),bw=std::max(90,(r.w-2*pad-(count-1)*gap)/count);
        return {r.x+pad+index*(bw+gap),r.y+r.h-unit(65),bw,unit(49)};
    }
    void button(Rect r,const std::string& text,bool highlighted=false){
        fill(r.x,r.y,r.w,r.h,highlighted?0x426cb2:0x34445c);
        frame(r.x,r.y,r.w,r.h,highlighted?0xa5caff:0x7888a0);
        int tw=textWidth(text,true);drawText(r.x+std::max(unit(8),(r.w-tw)/2),midBaseline(r.y,r.h),text,0xffffff,true,r.w-unit(12));
    }
    void refreshEntries(){
        entries.clear();selectedEntry=-1;scrollOffset=0;
        try{
            if(!std::filesystem::is_directory(browseDir))browseDir=std::filesystem::current_path();
            std::vector<Entry> dirs,files;
            for(auto& de:std::filesystem::directory_iterator(browseDir,std::filesystem::directory_options::skip_permission_denied)){
                std::error_code ec;const bool isDir=de.is_directory(ec);
                if(!isDir&&!de.is_regular_file(ec))continue;
                auto p=de.path();auto ext=p.extension().string();
                std::transform(ext.begin(),ext.end(),ext.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
                if(!isDir&&ext!=".sfc"&&ext!=".smc"&&ext!=".zip"&&ext!=".fig"&&ext!=".swc"&&ext!=".bs")continue;
                Entry entry{p.filename().string(),p,isDir};
                (isDir?dirs:files).push_back(std::move(entry));
            }
            auto sort=[](const Entry&a,const Entry&b){return a.label<b.label;};
            std::sort(dirs.begin(),dirs.end(),sort);std::sort(files.begin(),files.end(),sort);
            entries.reserve(dirs.size()+files.size());
            entries.insert(entries.end(),dirs.begin(),dirs.end());entries.insert(entries.end(),files.begin(),files.end());
        }catch(const std::exception& e){error=std::string("Browse error: ")+e.what();}
        needsRedraw=true;
    }
    // Native OS file chooser via the installed GTK/KDE dialog provider.
    // fork/exec avoids shell interpolation and runs asynchronously so the
    // X11 event loop stays responsive while the OS owns the picker window.
    static std::string onPath(const char* command){
        const char* path=std::getenv("PATH");if(!path)return {};
        std::string list(path), name(command);
        std::size_t start=0;
        for(;;){auto end=list.find(':',start);
            auto dir=list.substr(start,end==std::string::npos?std::string::npos:end-start);
            const std::string full=(dir.empty()?".":dir)+"/"+name;
            if(access(full.c_str(),X_OK)==0)return full;
            if(end==std::string::npos)break;
            start=end+1;
        }return {};
    }
    void openNativeBrowser(){
        activeMenu=-1;
        if(chooserPid>0)return;
        enum {Native,Zenity,Kdialog,Yad} kind=Native;
        std::string executable;
        // Prefer our bundled GTK file chooser, which uses the OS native GTK
        // dialog and does not require a separately installed zenity command.
        char exePath[4096];ssize_t length=readlink("/proc/self/exe",exePath,sizeof(exePath)-1);
        if(length>0){exePath[length]=0;auto bin=std::filesystem::path(exePath).parent_path();
            auto chooser=bin/"CipherSNES-NativePicker";
            if(access(chooser.c_str(),X_OK)==0)executable=chooser.string();}
        if(executable.empty()){executable=onPath("zenity");kind=Zenity;}
        if(executable.empty()){executable=onPath("kdialog");kind=Kdialog;}
        if(executable.empty()){executable=onPath("yad");kind=Yad;}
        if(executable.empty()){
            error="No native chooser found; install zenity (sudo apt install zenity). Using built-in browser.";
            openBrowser();return;
        }
        int fds[2];if(pipe(fds)!=0){error="Native file picker pipe failed";needsRedraw=true;return;}
        const pid_t pid=fork();
        if(pid<0){close(fds[0]);close(fds[1]);error="Could not launch OS file chooser";needsRedraw=true;return;}
        if(pid==0){
            close(fds[0]);dup2(fds[1],STDOUT_FILENO);close(fds[1]);
            const std::string initial=browseDir.string()+"/";
            if(kind==Native)execl(executable.c_str(),executable.c_str(),initial.c_str(),(char*)nullptr);
            if(kind==Zenity)execl(executable.c_str(),executable.c_str(),
                "--file-selection","--title=Open SNES ROM",
                "--file-filter=SNES ROMs | *.sfc *.smc *.zip *.swc *.fig *.bs",
                "--file-filter=All files | *",("--filename="+initial).c_str(),(char*)nullptr);
            if(kind==Kdialog)execl(executable.c_str(),executable.c_str(),
                "--getopenfilename",initial.c_str(),
                "SNES ROMs (*.sfc *.smc *.zip *.swc *.fig *.bs)",
                "--title","Open SNES ROM",(char*)nullptr);
            if(kind==Yad)execl(executable.c_str(),executable.c_str(),
                "--file","--title=Open SNES ROM",
                "--file-filter=SNES ROMs | *.sfc *.smc *.zip *.swc *.fig *.bs",
                ("--filename="+initial).c_str(),(char*)nullptr);
            _exit(127);
        }
        close(fds[1]);chooserFd=fds[0];
        fcntl(chooserFd,F_SETFL,fcntl(chooserFd,F_GETFL)|O_NONBLOCK);
        fcntl(chooserFd,F_SETFD,FD_CLOEXEC);
        chooserPid=pid;chooserData.clear();needsRedraw=true;
        if(sdl.device)sdl.clearAudio(sdl.device);
    }
    void pollNativeBrowser(){
        if(chooserPid<=0)return;
        char buffer[512];ssize_t n;
        while((n=read(chooserFd,buffer,sizeof(buffer)))>0)
            if(chooserData.size()<8192)chooserData.append(buffer,std::min<std::size_t>(n,8192-chooserData.size()));
        int status=0;pid_t done=waitpid(chooserPid,&status,WNOHANG);
        if(done==0)return;
        if(chooserFd>=0){close(chooserFd);chooserFd=-1;}
        chooserPid=-1;needsRedraw=true;pacer.reset(nowNs());
        if(done<0){error="Native file chooser exited unexpectedly";return;}
        if(WIFEXITED(status)&&WEXITSTATUS(status)==0){
            while(!chooserData.empty()&&(chooserData.back()=='\n'||chooserData.back()=='\r'))chooserData.pop_back();
            if(!chooserData.empty()){
                auto file=std::filesystem::path(chooserData);
                if(std::filesystem::is_regular_file(file)){
                    browseDir=file.parent_path();openRom(file.string());
                }else error="Selected file not found: "+chooserData;
            }
        }else if(WIFEXITED(status)&&WEXITSTATUS(status)==127)
            error="Native chooser failed to launch; install zenity";
        // Cancel/close is not an error.
    }
    void openBrowser(){dialog=Dialog::Browser;activeMenu=-1;error.clear();editingPath=false;
        if(!std::filesystem::is_directory(browseDir))browseDir=".";
        pathText="";refreshEntries();}
    int browserRows(Rect r)const{return std::max(1,(r.h-unit(245))/rowHeight);}
    Rect browserList(Rect r)const{return {r.x+unit(20),r.y+unit(132),r.w-unit(40),browserRows(r)*rowHeight};}
    void drawBrowser(Rect r){
        drawText(r.x+unit(24),r.y+unit(54),"Open SNES ROM",0xffffff,true);
        drawText(r.x+unit(24),r.y+unit(89),browseDir.string(),0xb2cfff,false,r.w-unit(50));
        Rect list=browserList(r);
        fill(list.x,list.y,list.w,list.h,0x111825);frame(list.x,list.y,list.w,list.h,0x657289);
        for(int i=0;i<browserRows(r);i++){
            int idx=i+scrollOffset;if(idx>=static_cast<int>(entries.size()))break;
            int y=list.y+i*rowHeight;
            if(idx==selectedEntry)fill(list.x+2,y+2,list.w-4,rowHeight-3,0x355e9f);
            std::string s=std::string(entries[idx].directory?"[DIR]   ":"[ROM]   ")+entries[idx].label;
            drawText(list.x+unit(14),midBaseline(y,rowHeight),s,
                     entries[idx].directory?0xc3dfed:0xeaf1f9,false,list.w-unit(35));
        }
        drawText(r.x+unit(24),r.y+r.h-unit(92),
            std::string(editingPath?"Typing path: ":"Path (Ctrl+L): ")+pathText,
            editingPath?0xffe7a3:0xdbe6f2,false,r.w-unit(48));
        for(int i=0;i<4;i++)button(buttonRect(i,4,r),std::array<std::string,4>{"Up","Downloads","Open","Cancel"}[i],i==2);
    }
    int controllerStep(Rect r)const{
        return std::clamp((r.h-unit(210))/6,unit(33),unit(51));
    }
    void drawController(Rect r){
        drawText(r.x+unit(24),r.y+unit(53),"Controller Settings",0xffffff,true);
        drawText(r.x+unit(24),r.y+unit(86),sdl.padOk?sdl.padName:sdl.status,0xc2d4eb,false,r.w-unit(48));
        drawText(r.x+unit(24),r.y+unit(113),padMappingMode?
            "GAMEPAD mapping: click button, then press pad button":"KEYBOARD mapping: click button, then press key",
            0xb2cfff,false,r.w-unit(48));
        const int step=controllerStep(r), top=r.y+unit(144);
        const int colWidth=(r.w-unit(45))/2;
        for(int i=0;i<static_cast<int>(buttonCount);i++){
            const int column=i/6,row=i%6;
            const int x=r.x+unit(24)+column*colWidth;
            const int y=top+row*step;
            if(captureButton==i)fill(x-unit(4),y-unit(27),colWidth-unit(12),step,0x4264a0);
            drawText(x,y,buttonName[i],0xffffff,true);
            const char* symbol=XKeysymToString(keys[i]);
            const std::string keyName=(captureButton==i)?
                (padMappingMode?"Press pad button...":"Press key (Esc cancels)"):
                (padMappingMode?("Button "+std::to_string(padMapping[i])):(symbol?symbol:"Unknown"));
            drawText(x+unit(100),y,keyName,0xb2d8ff,false,colWidth-unit(115));
        }
        for(int i=0;i<4;i++)button(buttonRect(i,4,r),
            std::array<std::string,4>{padMappingMode?"Gamepad":"Keyboard","Defaults","Save","Close"}[i],i==2);
    }
    void drawAbout(Rect r){
        drawText(r.x+unit(24),r.y+unit(55),"CipherSNES  v0.1.0",0xffffff,true);
        const int imageX=r.x+unit(24),imageY=r.y+unit(95);
        drawAboutIcon(imageX,imageY);
        const int textX=imageX+aboutIconW+unit(26);
        const int avail=std::max(0,r.x+r.w-textX-unit(24));
        drawText(textX,r.y+unit(116),"HodgkinsStudios",0xc4daff,true,avail);
        drawText(textX,r.y+unit(158),"Super Nintendo emulator",0xf1f4ff,false,avail);
        drawText(textX,r.y+unit(199),"C++17  |  Linux  |  X11",0xd9e2ef,false,avail);
        drawText(textX,r.y+unit(240),"Hardware-accuracy development",0xd9e2ef,false,avail);
        if(r.h<unit(510)){
            drawText(r.x+unit(24),r.y+r.h-unit(100),"Ctrl+O: Open   F5: Reset   F9: Pause",0xd9e2ef,false,r.w-unit(45));
            drawText(r.x+unit(24),r.y+r.h-unit(75),"F11: Fullscreen   Esc: Windowed",0xd9e2ef,false,r.w-unit(45));
        }else{
            drawText(r.x+unit(24),r.y+r.h-unit(160),"Ctrl+O: Load ROM   F5: Reset   F9: Pause",0xd9e2ef,false,r.w-unit(45));
            drawText(r.x+unit(24),r.y+r.h-unit(122),"F11: Fullscreen   Esc: Windowed   Config: Inputs",0xd9e2ef,false,r.w-unit(45));
            drawText(r.x+unit(24),r.y+r.h-unit(87),"Open-source project. No game ROMs included.",0xadc2dc,false,r.w-unit(45));
        }
        button(buttonRect(0,1,r),"Close",true);
    }
    void drawDialog(){
        if(dialog==Dialog::Closed)return;
        Rect r=dialogRect();
        fill(0,menuHeight,width,height-menuHeight,0x111723);
        fill(r.x,r.y,r.w,r.h,0x283448);frame(r.x,r.y,r.w,r.h,0x84a9e1);
        if(dialog==Dialog::Browser)drawBrowser(r);
        else if(dialog==Dialog::Controller)drawController(r);
        else drawAbout(r);
    }
    VideoRegion drawVideo(){
        if(!loaded){
            drawText(unit(35),menuHeight+unit(76),"Welcome to CipherSNES",0xf5f7ff,true);
            drawText(unit(35),menuHeight+unit(126),"Choose File > Load SNES ROM to begin.",0xcbd8eb);
            drawText(unit(35),menuHeight+unit(169),"You can also press Ctrl+O or drag a ROM path as an argument.",0xabbcd4);
            return {};
        }
        auto video=core.video();
        if(!video.pixels||!video.width||!video.height)return {};
        const VideoRegion region=calculateVideoRegion(width,height,menuHeight,video.width,video.height);
        if(!region.valid())return {};
        const int w=region.w,h=region.h;
        const auto factor=region.scale;
        const auto n=static_cast<std::size_t>(w)*h;
        if(scaled.size()!=n){freeImage();scaled.assign(n,0);}
        // CP68: expand each horizontal row once, then duplicate the completed
        // row with memcpy. Exact nearest-neighbour pixels, less CPU work at
        // 2x/3x/4x integer scales (including large fullscreen windows).
        scaleNearest32(video.pixels, video.pitchBytes, video.width, video.height,
                       static_cast<unsigned>(factor), scaled.data());
        if(!image)image=XCreateImage(display,DefaultVisual(display,DefaultScreen(display)),
                        static_cast<unsigned>(DefaultDepth(display,DefaultScreen(display))),ZPixmap,
                        0,reinterpret_cast<char*>(scaled.data()),w,h,32,static_cast<int>(w*sizeof(std::uint32_t)));
        if(image){
            XPutImage(display,paintTarget(),gc,image,0,0,region.x,region.y,w,h);
            return region;
        }
        return {};
    }
    // CP69: fullscreen-sized XCopyArea operations and repeating static UI
    // painting can dominate server time. When only the emulated picture has
    // advanced and its rectangle is unchanged, copy ONLY its exact rectangle.
    // Expose, resizing, resolution changes, menus, dialogs, and errors always
    // trigger a full composition; no stale letterbox/menu pixels are reused.
    void draw(bool videoOnly=false){
        if(!mapped)return;
        ensureBackbuffer();
        VideoRegion current{};
        if(videoOnly && loaded && dialog==Dialog::Closed && activeMenu<0 && error.empty()){
            const auto video=core.video();
            if(video.pixels)
                current=calculateVideoRegion(width,height,menuHeight,video.width,video.height);
            if(canPresentVideoOnly(false,previousVideoValid,previousVideoRegion,current)){
                const VideoRegion updated=drawVideo();
                if(updated.valid()){
                    XCopyArea(display,backbuffer,window,gc,updated.x,updated.y,
                        static_cast<unsigned>(updated.w),static_cast<unsigned>(updated.h),
                        updated.x,updated.y);
                    ++videoOnlyCopies;
                    XFlush(display);
                    needsRedraw=false;
                    return;
                }
            }
        }
        fill(0,0,width,height,0x0e131d);
        previousVideoRegion={};
        previousVideoValid=false;
        if(dialog==Dialog::Closed){
            previousVideoRegion=drawVideo();
            previousVideoValid=previousVideoRegion.valid();
        }
        drawBar();
        if(dialog!=Dialog::Closed)drawDialog();
        else drawDropdown();
        if(!error.empty()){
            fill(0,height-unit(44),width,unit(44),0x623037);
            drawText(unit(15),midBaseline(height-unit(44),unit(44)),error,0xffe4e8,false,width-unit(30));
        }
        // Whole-window presentation is necessary after *static* UI damage.
        XCopyArea(display,backbuffer,window,gc,0,0,
            static_cast<unsigned>(width),static_cast<unsigned>(height),0,0);
        ++fullCopies;
        XFlush(display);needsRedraw=false;
    }
    void selectFile(){
        if(selectedEntry<0||selectedEntry>=static_cast<int>(entries.size()))return;
        auto e=entries[selectedEntry];
        if(e.directory){browseDir=e.path;refreshEntries();}
        else openRom(e.path.string());
    }
    // The EWMH WM owns the actual mode change. Both View/F11 and Escape use
    // the same request path; no resizing hacks or emulator-timing changes.
    void requestFullscreen(bool enable){
        if(fullscreen==enable)return;
        fullscreen=enable;
        activeMenu=-1;
        hoverItem=-1;
        Atom wmState=XInternAtom(display,"_NET_WM_STATE",False);
        Atom wmFull=XInternAtom(display,"_NET_WM_STATE_FULLSCREEN",False);
        XEvent e{};
        e.xclient.type=ClientMessage;
        e.xclient.window=window;
        e.xclient.message_type=wmState;
        e.xclient.format=32;
        e.xclient.data.l[0]=enable?1:0; // _NET_WM_STATE_ADD or REMOVE
        e.xclient.data.l[1]=static_cast<long>(wmFull);
        e.xclient.data.l[3]=1; // source indication: application
        XSendEvent(display,DefaultRootWindow(display),False,
            SubstructureRedirectMask|SubstructureNotifyMask,&e);
        previousVideoValid=false;
        needsRedraw=true;
        XFlush(display);
    }
    // Keep Escape meaningful even if fullscreen was changed externally by WM.
    void syncFullscreenState(){
        Atom wmState=XInternAtom(display,"_NET_WM_STATE",False);
        Atom wmFull=XInternAtom(display,"_NET_WM_STATE_FULLSCREEN",False);
        Atom type=None;
        int fmt=0;
        unsigned long count=0,bytesRemaining=0;
        unsigned char* property=nullptr;
        if(XGetWindowProperty(display,window,wmState,0,32,False,XA_ATOM,
             &type,&fmt,&count,&bytesRemaining,&property)!=Success)return;
        bool isFull=false;
        if(type==XA_ATOM && fmt==32 && property){
            auto* atoms=reinterpret_cast<Atom*>(property);
            for(unsigned long n=0;n<count;++n)if(atoms[n]==wmFull)isFull=true;
        }
        if(property)XFree(property);
        if(fullscreen!=isFull){fullscreen=isFull;previousVideoValid=false;needsRedraw=true;}
    }
    void menuAction(int m,int i){
        activeMenu=-1;hoverItem=-1;
        switch(m){
            case 0:
                if(i==0)openNativeBrowser();
                if(i==1)reset();
                if(i==2)running=false;
                break;
            case 1:
                if(i==0){dialog=Dialog::Controller;scrollOffset=0;captureButton=-1;}
                if(i==1){sdl.scan(true);shownPadName=sdl.padName;error=sdl.available()?"Gamepad connected: "+sdl.padName:"No gamepad detected (check USB/Bluetooth/permissions)";}
                if(i==2){audioEnabled=!audioEnabled;if(sdl.device){sdl.pauseAudio(sdl.device,audioEnabled?0:1);if(!audioEnabled)sdl.clearAudio(sdl.device);}saveSettings();}
                break;
            case 2:if(i==0)requestFullscreen(!fullscreen);break;
            case 3:if(i==0)dialog=Dialog::About;break;
        }
        needsRedraw=true;
    }
    void key(XKeyEvent ev,bool down){
        KeySym symbol=XLookupKeysym(&ev,0);
        if(!down){for(unsigned i=0;i<buttonCount;i++)if(symbol==keys[i])held[i]=false;return;}
        if(dialog==Dialog::Controller&&captureButton>=0&&!padMappingMode){
            if(symbol!=XK_Escape){keys[static_cast<unsigned>(captureButton)]=symbol;saveSettings();}
            captureButton=-1;needsRedraw=true;return;
        }
        if(dialog!=Dialog::Closed){
            if(dialog==Dialog::Controller&&padMappingMode&&captureButton>=0&&symbol==XK_Escape){
                captureButton=-1;needsRedraw=true;return;
            }
            if(symbol==XK_Escape&&!(dialog==Dialog::Browser&&editingPath)){dialog=Dialog::Closed;needsRedraw=true;return;}
            if(dialog==Dialog::Browser){
                if(editingPath){
                    if(symbol==XK_Escape){editingPath=false;needsRedraw=true;return;}
                    if(symbol==XK_BackSpace){if(!pathText.empty())pathText.pop_back();needsRedraw=true;return;}
                    if(symbol==XK_Return||symbol==XK_KP_Enter){
                        auto p=std::filesystem::path(pathText);
                        if(std::filesystem::is_directory(p)){browseDir=p;refreshEntries();editingPath=false;}
                        else if(std::filesystem::is_regular_file(p)){editingPath=false;openRom(p.string());}
                        else {error="File not found: "+pathText;needsRedraw=true;}
                        return;
                    }
                    char buf[64];KeySym keySym{};
                    int n=XLookupString(&ev,buf,sizeof(buf),&keySym,nullptr);
                    if(n>0&&!(ev.state&ControlMask)){pathText.append(buf,n);needsRedraw=true;}
                    return;
                }
                if((ev.state&ControlMask)&&(symbol==XK_l||symbol==XK_L)){
                    pathText=browseDir.string()+"/";editingPath=true;needsRedraw=true;return;
                }
                if(symbol==XK_BackSpace){browseDir=browseDir.parent_path();refreshEntries();return;}
                if(symbol==XK_Up||symbol==XK_Down){
                    int count=static_cast<int>(entries.size());if(count==0)return;
                    selectedEntry=std::clamp(selectedEntry+(symbol==XK_Up?-1:1),0,count-1);
                    Rect r=dialogRect();int rows=browserRows(r);
                    if(selectedEntry<scrollOffset)scrollOffset=selectedEntry;
                    if(selectedEntry>=scrollOffset+rows)scrollOffset=selectedEntry-rows+1;
                    pathText=entries[selectedEntry].path.string();needsRedraw=true;return;
                }
                if(symbol==XK_Return||symbol==XK_KP_Enter){selectFile();return;}
            }
            return;
        }
        if(activeMenu>=0){
            auto labels=menuLabels(activeMenu);
            if(symbol==XK_Escape){
                activeMenu=-1;
                if(fullscreen)requestFullscreen(false);
                needsRedraw=true;return;
            }
            if(symbol==XK_Left||symbol==XK_Right){activeMenu=(activeMenu+(symbol==XK_Right?1:3))%4;hoverItem=0;needsRedraw=true;return;}
            if(symbol==XK_Up||symbol==XK_Down){hoverItem=(hoverItem+(symbol==XK_Down?1:static_cast<int>(labels.size())-1))%static_cast<int>(labels.size());needsRedraw=true;return;}
            if(symbol==XK_Return||symbol==XK_KP_Enter){menuAction(activeMenu,std::max(0,hoverItem));return;}
        }
        if((ev.state&Mod1Mask)&&(symbol==XK_f||symbol==XK_F||symbol==XK_c||symbol==XK_C||symbol==XK_v||symbol==XK_V||symbol==XK_h||symbol==XK_H)){
            if(symbol==XK_f||symbol==XK_F)activeMenu=0;
            if(symbol==XK_c||symbol==XK_C)activeMenu=1;
            if(symbol==XK_v||symbol==XK_V)activeMenu=2;
            if(symbol==XK_h||symbol==XK_H)activeMenu=3;
            hoverItem=0;needsRedraw=true;return;
        }
        if((ev.state&ControlMask)&&(symbol==XK_o||symbol==XK_O)){openNativeBrowser();return;}
        if((ev.state&ControlMask)&&(symbol==XK_q||symbol==XK_Q)){running=false;return;}
        if(symbol==XK_F5){reset();return;}
        if(symbol==XK_F9){paused=!paused;pacer.reset(nowNs());needsRedraw=true;return;}
        if(symbol==XK_F11){requestFullscreen(!fullscreen);return;}
        // Escape exits fullscreen when emulation is active, never entering it
        // from a normal window. Dialogs and key-remapping still handle their
        // own Escape earlier in this function.
        if(symbol==XK_Escape){if(fullscreen)requestFullscreen(false);return;}
        for(unsigned i=0;i<buttonCount;i++)if(symbol==keys[i])held[i]=true;
    }
    void click(int x,int y,unsigned button){
        if(button==4||button==5){
            if(dialog==Dialog::Browser){Rect r=browserList(dialogRect());if(r.contains(x,y)){
                    scrollOffset=std::clamp(scrollOffset+(button==4?-3:3),0,std::max(0,static_cast<int>(entries.size())-browserRows(dialogRect())));
                    needsRedraw=true;}}
            else if(dialog==Dialog::Controller){scrollOffset=std::clamp(scrollOffset+(button==4?-1:1),0,4);needsRedraw=true;}
            return;
        }
        if(button!=1)return;
        for(int i=0;i<4;i++)if(menuHeader(i).contains(x,y)){
            activeMenu=(activeMenu==i)?-1:i;hoverItem=0;needsRedraw=true;return;
        }
        if(activeMenu>=0){Rect r=dropdown(activeMenu);
            if(r.contains(x,y)){
                int idx=(y-r.y-unit(6))/rowHeight;
                auto labels=menuLabels(activeMenu);
                if(y>=r.y+unit(6)&&idx>=0&&idx<static_cast<int>(labels.size()))menuAction(activeMenu,idx);
            }else{activeMenu=-1;needsRedraw=true;}
            return;
        }
        if(dialog==Dialog::Closed)return;
        Rect r=dialogRect();
        if(dialog==Dialog::Browser){
            Rect list=browserList(r);
            if(list.contains(x,y)){
                int idx=scrollOffset+(y-list.y)/rowHeight;
                if(idx>=0&&idx<static_cast<int>(entries.size())){
                    auto now=nowNs();
                    if(selectedEntry==idx&&lastClickEntry==idx&&now-lastClickNs<450000000LL)selectFile();
                    else{selectedEntry=idx;pathText=entries[idx].path.string();needsRedraw=true;}
                    lastClickEntry=idx;lastClickNs=now;
                }return;
            }
            for(int i=0;i<4;i++)if(buttonRect(i,4,r).contains(x,y)){
                if(i==0){browseDir=browseDir.parent_path();refreshEntries();}
                if(i==1){if(const char* h=std::getenv("HOME"))browseDir=std::filesystem::path(h)/"Downloads";refreshEntries();}
                if(i==2)selectFile();
                if(i==3)dialog=Dialog::Closed;
                needsRedraw=true;return;
            }
        }else if(dialog==Dialog::Controller){
            const int step=controllerStep(r),top=r.y+unit(144);
            const int colWidth=(r.w-unit(45))/2;
            const int x0=r.x+unit(24);
            if(x>=x0&&x<x0+colWidth*2&&y>=top-unit(28)&&y<top+5*step+unit(12)){
                const int column=(x-x0)/colWidth;
                const int row=std::clamp((y-(top-unit(28)))/step,0,5);
                captureButton=column*6+row;needsRedraw=true;return;
            }
            for(int i=0;i<4;i++)if(buttonRect(i,4,r).contains(x,y)){
                if(i==0){padMappingMode=!padMappingMode;captureButton=-1;}
                if(i==1){if(padMappingMode)padMapping=kPad;else keys=kKeys;
                    captureButton=-1;saveSettings();}
                if(i==2){saveSettings();dialog=Dialog::Closed;}
                if(i==3){captureButton=-1;dialog=Dialog::Closed;}
                needsRedraw=true;return;
            }
        }else if(dialog==Dialog::About&&buttonRect(0,1,r).contains(x,y)){
            dialog=Dialog::Closed;needsRedraw=true;return;
        }
    }
    void mouseMove(int x,int y){
        if(activeMenu<0)return;
        for(int i=0;i<4;i++)if(menuHeader(i).contains(x,y)){
            if(activeMenu!=i){activeMenu=i;hoverItem=0;needsRedraw=true;}return;
        }
        Rect r=dropdown(activeMenu);
        int item=-1;if(r.contains(x,y)&&y>=r.y+unit(6)){
            item=(y-r.y-unit(6))/rowHeight;
            if(item>=static_cast<int>(menuLabels(activeMenu).size()))item=-1;
        }
        if(item!=hoverItem){hoverItem=item;needsRedraw=true;}
    }
    void tickPad(){
        sdl.tick();
        auto now=nowNs();
        if(now-lastPadScan>300000000LL||!sdl.available()){
            // Throttle scans on systems without controllers; still detect
            // devices plugged in after launching the emulator.
            if(now-lastPadScan>300000000LL){sdl.scan();lastPadScan=now;}
        }
        if(shownPadName!=sdl.padName){shownPadName=sdl.padName;needsRedraw=true;}
        // Rising-edge detection for user-initiated gamepad remapping.
        for(int b=0;b<static_cast<int>(priorPadButtons.size());b++){
            const bool down=sdl.button(b);
            if(dialog==Dialog::Controller&&padMappingMode&&captureButton>=0&&
               down&&!priorPadButtons[b]){
                padMapping[static_cast<unsigned>(captureButton)]=b;
                captureButton=-1;saveSettings();needsRedraw=true;
            }
            priorPadButtons[b]=down;
        }
    }
    void updateInput(){
        for(unsigned i=0;i<buttonCount;i++){
            bool pressed=hasFocus&&held[i];
            if(sdl.available()){
                pressed|=sdl.button(padMapping[i]);
                if(i>=4&&i<=7)pressed|=sdl.direction(static_cast<int>(i));
            }
            core.setButton(0,static_cast<SnesButton>(i),pressed);
        }
    }
    void run(){
        // Opt-in, low-overhead host presentation diagnostics. No clocks or
        // emulated frames depend on these measurements. Kept off by default.
        const bool presentationStatsEnabled = std::getenv("CIPHERSNES_PRESENT_STATS") != nullptr;
        std::array<std::int64_t,128> presentDurations{};
        unsigned presentSamples=0, catchupFrames=0;
        while(running){
            while(XPending(display)){
                XEvent ev{};XNextEvent(display,&ev);
                switch(ev.type){
                case Expose:needsRedraw=true;break;
                case PropertyNotify:
                    if(ev.xproperty.atom==XInternAtom(display,"_NET_WM_STATE",False))
                        syncFullscreenState();
                    break;
                case ConfigureNotify:
                    if(width!=ev.xconfigure.width||height!=ev.xconfigure.height){
                        width=ev.xconfigure.width;height=ev.xconfigure.height;needsRedraw=true;
                    }break;
                case MapNotify:mapped=true;needsRedraw=true;break;
                case UnmapNotify:mapped=false;break;
                case FocusOut:hasFocus=false;held.fill(false);activeMenu=-1;needsRedraw=true;break;
                case FocusIn:hasFocus=true;break;
                case ButtonPress:click(ev.xbutton.x,ev.xbutton.y,ev.xbutton.button);break;
                case MotionNotify:mouseMove(ev.xmotion.x,ev.xmotion.y);break;
                case KeyPress:key(ev.xkey,true);break;
                case KeyRelease:
                    if(XPending(display)) {XEvent next;XPeekEvent(display,&next);
                        if(next.type==KeyPress&&next.xkey.keycode==ev.xkey.keycode&&
                           next.xkey.time==ev.xkey.time)break;}
                    key(ev.xkey,false);break;
                case ClientMessage:if(static_cast<Atom>(ev.xclient.data.l[0])==wmDelete)running=false;break;
                default:break;
                }
            }
            tickPad();
            pollNativeBrowser();
            auto now=nowNs();
            bool videoOnlyCandidate=false;
            if(loaded&&!paused&&chooserPid<=0&&dialog==Dialog::Closed&&activeMenu<0&&!core.stopped()&&!core.faulted()){
                unsigned due=pacer.framesDue(now);
                if (presentationStatsEnabled && due > 1u) catchupFrames += due - 1u;
                bool rendered=false;
                for(unsigned f=0;f<due;f++){
                    updateInput();
                    const auto before=core.frameCount();
                    core.runFrame(250000u, f+1==due && mapped);
                    if(core.frameCount()==before){error="Emulation stalled";paused=true;break;}
                    pacer.completedFrame();++sampleFrames;
                    if(audioEnabled){core.takeAudioInto(sound);
                        if(sdl.device&&!sound.empty()){
                            constexpr U32 maxQueue=32000u*2u*sizeof(std::int16_t)/5u;
                            if(sdl.queuedSize(sdl.device)>maxQueue)sdl.clearAudio(sdl.device);
                            sdl.queueAudio(sdl.device,sound.data(),static_cast<U32>(sound.size()*sizeof(std::int16_t)));
                        }
                    }else core.discardAudio();
                    rendered|=(f+1==due);
                    if(core.faulted()||core.stopped())break;
                }
                // Existing input/expose/dialog damage takes precedence over a
                // video-only present; do not suppress any needed UI redraw.
                videoOnlyCandidate=rendered && !needsRedraw;
                needsRedraw|=rendered;
                if(sampleStart==0)sampleStart=now;
                if(now-sampleStart>1000000000LL){
                    const double fps=sampleFrames/((now-sampleStart)/1e9);
                    const double expected=core.isPal()?50.0069789:60.0988139;
                    char suffix[96];std::snprintf(suffix,sizeof(suffix),"  %.1f FPS / %d%%",fps,
                        static_cast<int>(std::min(100.0,100*fps/expected)));
                    XStoreName(display,window,(title+suffix).c_str());
                    if(presentationStatsEnabled){
                        const unsigned n=std::min<unsigned>(presentSamples,static_cast<unsigned>(presentDurations.size()));
                        std::array<std::int64_t,128> sorted{};
                        for (unsigned i=0; i<n; ++i)
                            sorted[i]=presentDurations[(presentSamples-n+i)%presentDurations.size()];
                        std::sort(sorted.begin(),sorted.begin()+n);
                        const double p50=n?sorted[(n-1u)*50u/100u]/1e6:0.0;
                        const double p95=n?sorted[(n-1u)*95u/100u]/1e6:0.0;
                        const double p99=n?sorted[(n-1u)*99u/100u]/1e6:0.0;
                        const U32 queuedBytes=(sdl.device&&sdl.queuedSize)?sdl.queuedSize(sdl.device):0u;
                        const double queuedMs=queuedBytes*1000.0/(32000.0*2.0*sizeof(std::int16_t));
                        std::fprintf(stderr,"PRESENT_STATS mode=%s viewport=%dx%d full=%lu video_only=%lu fps=%.2f "
                            "present_p50_ms=%.3f present_p95_ms=%.3f present_p99_ms=%.3f "
                            "audio_queue_ms=%.1f catchup_frames=%u\n",
                            fullscreen?"fullscreen":"windowed",width,height,
                            fullCopies,videoOnlyCopies,fps,p50,p95,p99,queuedMs,catchupFrames);
                        presentSamples=0;
                        catchupFrames=0;
                    }
                    fullCopies=0;videoOnlyCopies=0;
                    sampleFrames=0;sampleStart=now;
                }
                if(core.faulted()){error=core.faultReason();paused=true;needsRedraw=true;}
            }else if(loaded&&(dialog!=Dialog::Closed||activeMenu>=0||chooserPid>0)){
                // Opening a menu does not let the emulator silently catch up a large queue.
                pacer.reset(now);
                if(sdl.device)sdl.clearAudio(sdl.device);
            }
            if(needsRedraw){
                const auto started=presentationStatsEnabled?nowNs():0;
                draw(videoOnlyCandidate);
                if(presentationStatsEnabled)
                    presentDurations[presentSamples++%presentDurations.size()]=nowNs()-started;
            }
            now=nowNs();
            pollfd fd{ConnectionNumber(display),POLLIN,0};
            if(loaded&&!paused&&chooserPid<=0&&dialog==Dialog::Closed&&activeMenu<0&&!core.stopped()&&!core.faulted()){
                // CP68: poll() rounded every frame deadline up to a whole
                // millisecond, causing avoidable presentation jitter. ppoll()
                // waits for the actual monotonic pacer deadline at nanosecond
                // granularity while still waking immediately for X11 input.
                // This controls *presentation only*, not emulated clocks.
                const auto ahead=std::clamp<std::int64_t>(pacer.nextFrameNs()-now,0,21000000LL);
                const timespec timeout{static_cast<time_t>(ahead/1000000000LL),
                                       static_cast<long>(ahead%1000000000LL)};
                if(ppoll(&fd,1,&timeout,nullptr)<0 && errno!=EINTR)break;
            } else {
                if(poll(&fd,1,25)<0 && errno!=EINTR)break;
            }
        }
    }
};
}
int main(int argc,char** argv){
    try{App app;if(argc>1)app.openRom(argv[1]);app.run();return 0;}
    catch(const std::exception& ex){std::fprintf(stderr,"CipherSNES X11: %s\n",ex.what());return 1;}
}
