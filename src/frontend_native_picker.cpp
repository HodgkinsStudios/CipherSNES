// CipherSNES native GTK3 file chooser helper (runtime-only GTK dependency).
// Kept in its own process so the X11 emulation event loop does not need GTK
// headers, GTK main-loop integration, or additional threads.
#include <dlfcn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

struct GtkWidget;
struct GtkFileFilter;
static void* symbol(void* lib, const char* name){
    auto* fn=dlsym(lib,name);
    if(!fn)std::fprintf(stderr,"CipherSNES picker: missing %s\n",name);
    return fn;
}
int main(int argc,char** argv){
    void* gtk=dlopen("libgtk-3.so.0",RTLD_NOW|RTLD_LOCAL);
    void* glib=dlopen("libglib-2.0.so.0",RTLD_NOW|RTLD_LOCAL);
    if(!gtk||!glib){std::fprintf(stderr,"CipherSNES picker: GTK3 runtime unavailable\n");return 127;}
    using Init=int(*)(int*,char***);
    using New=GtkWidget*(*)(const char*,GtkWidget*,int,...);
    using DialogRun=int(*)(GtkWidget*);
    using GetFilename=char*(*)(GtkWidget*);
    using Destroy=void(*)(GtkWidget*);
    using Free=void(*)(void*);
    using SetFolder=int(*)(GtkWidget*,const char*);
    using FilterNew=GtkFileFilter*(*)();
    using FilterSetName=void(*)(GtkFileFilter*,const char*);
    using FilterPattern=void(*)(GtkFileFilter*,const char*);
    using AddFilter=void(*)(GtkWidget*,GtkFileFilter*);
    using SetSize=void(*)(GtkWidget*,int,int);
    auto init=reinterpret_cast<Init>(symbol(gtk,"gtk_init_check"));
    auto create=reinterpret_cast<New>(symbol(gtk,"gtk_file_chooser_dialog_new"));
    auto run=reinterpret_cast<DialogRun>(symbol(gtk,"gtk_dialog_run"));
    auto getFile=reinterpret_cast<GetFilename>(symbol(gtk,"gtk_file_chooser_get_filename"));
    auto destroy=reinterpret_cast<Destroy>(symbol(gtk,"gtk_widget_destroy"));
    auto release=reinterpret_cast<Free>(symbol(glib,"g_free"));
    auto setFolder=reinterpret_cast<SetFolder>(symbol(gtk,"gtk_file_chooser_set_current_folder"));
    auto newFilter=reinterpret_cast<FilterNew>(symbol(gtk,"gtk_file_filter_new"));
    auto setName=reinterpret_cast<FilterSetName>(symbol(gtk,"gtk_file_filter_set_name"));
    auto addPattern=reinterpret_cast<FilterPattern>(symbol(gtk,"gtk_file_filter_add_pattern"));
    auto addFilter=reinterpret_cast<AddFilter>(symbol(gtk,"gtk_file_chooser_add_filter"));
    auto setSize=reinterpret_cast<SetSize>(symbol(gtk,"gtk_window_set_default_size"));
    if(!init||!create||!run||!getFile||!destroy||!release||!setFolder||!newFilter||
       !setName||!addPattern||!addFilter||!setSize)return 127;
    if(!init(&argc,&argv)){std::fprintf(stderr,"CipherSNES picker: cannot initialize GTK3\n");return 127;}
    constexpr int OPEN=0, CANCEL=-6, ACCEPT=-3;
    GtkWidget* dialog=create("Open SNES ROM",nullptr,OPEN,"_Cancel",CANCEL,"_Open",ACCEPT,nullptr);
    if(!dialog)return 127;
    setSize(dialog,920,660);
    if(argc>1)setFolder(dialog,argv[1]);
    GtkFileFilter* rom=newFilter();setName(rom,"SNES ROMs (*.sfc, *.smc, *.zip, *.swc, *.fig, *.bs)");
    for(const char* ext:{"*.sfc","*.smc","*.zip","*.swc","*.fig","*.bs",
                         "*.SFC","*.SMC","*.ZIP","*.SWC","*.FIG","*.BS"})
        addPattern(rom,ext);
    addFilter(dialog,rom);
    GtkFileFilter* all=newFilter();setName(all,"All files");addPattern(all,"*");addFilter(dialog,all);
    const int result=run(dialog);
    int status=1;
    if(result==ACCEPT){char* filename=getFile(dialog);if(filename){
        std::printf("%s\n",filename);std::fflush(stdout);release(filename);status=0;}}
    destroy(dialog);
    return status;
}
