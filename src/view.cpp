#include <stdio.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <math.h>

using std::vector;
using std::string;

/* Event for when a new thumbnail is loaded */
const int VIEW_TYPE = ALLEGRO_GET_EVENT_TYPE('V', 'I', 'E', 'W');

/* Event for when a percent of the files searched is incremented by at least 1 */
const int PERCENT_TYPE = ALLEGRO_GET_EVENT_TYPE('P', 'R', 'C', 'T');

/* Event for when an image request is done loading */
const int LOAD_TYPE = ALLEGRO_GET_EVENT_TYPE('L', 'O', 'A', 'D');

// #define debug(...) printf(__VA_ARGS__)
#define debug(...)

ALLEGRO_MUTEX * globalQuit;
bool doQuit = false;

struct Image{
    Image(ALLEGRO_BITMAP * thumbnail, const string & name):
        thumbnail(thumbnail),
        video(NULL),
        filename(name){
        }

    ALLEGRO_BITMAP * thumbnail;
    /* Video copy of the thumbnail */
    ALLEGRO_BITMAP * video;
    string filename;
};

static bool sortImage(Image * a, Image * b){
    return a->filename < b->filename;
}

/* Loads images in the background and returns the current image when its available.
 *
 * There should be N worker threads, probably 2 or 3, that just loop waiting to be
 * given work. The work will be in the form of a filename that they should use to
 * load a bitmap with al_load_bitmap. This will return a memory bitmap that will be
 * sent back to the manager.
 *
 * The manager should create a mailbox that contains a mutex and a boolean that says
 * when the mailbox is full. The worker will place the memory bitmap in the mailbox.
 */
class ImageManager{
public:
    static const int MAX_WORKERS = 2;

    class Mailbox{
    public:
        Mailbox(const string & file, ALLEGRO_EVENT_SOURCE * events):
        file(file),
        count(0),
        events(events),
        bitmap(NULL){
            mutex = al_create_mutex();
        }

        ~Mailbox(){
            al_destroy_mutex(mutex);
        }

        void inc(){
            al_lock_mutex(mutex);
            this->count += 1;
            al_unlock_mutex(mutex);
        }

        void dec(){
            al_lock_mutex(mutex);
            this->count -= 1;
            al_unlock_mutex(mutex);
        }

        int getCount() const {
            int out = 0;
            al_lock_mutex(mutex);
            out = this->count;
            al_unlock_mutex(mutex);
            return out;
        }

        const string getFile() const {
            return file;
        }

        void setBitmap(ALLEGRO_BITMAP * bitmap){
            al_lock_mutex(mutex);
            this->bitmap = bitmap;
            al_unlock_mutex(mutex);

            if (bitmap != NULL){
                /* When the mailbox is loaded with a bitmap we output
                 * a load event to tell the main thread to redraw if necessary.
                 */
                ALLEGRO_EVENT event;
                event.user.type = LOAD_TYPE;
                al_emit_user_event(events, &event, NULL);
            }
        }

        ALLEGRO_BITMAP * getBitmap(){
            ALLEGRO_BITMAP * out = NULL;
            al_lock_mutex(mutex);
            out = this->bitmap;
            al_unlock_mutex(mutex);
            return out;
        }

        const string file;
        /* The number of tasks that reference this mailbox */
        int count;
        ALLEGRO_EVENT_SOURCE * events;
        ALLEGRO_MUTEX * mutex;
        ALLEGRO_BITMAP * bitmap;
    };

    class Task{
    public:
        Task(Mailbox * box):
        box(box){
            box->inc();
        }

        ~Task(){
            box->dec();
        }

        Mailbox * getBox(){
            return box;
        }

        Mailbox * box;
    };

    /* Contains a list of tasks that can be taken off by workers */
    class TaskList{
    public:
        TaskList(){
            mutex = al_create_mutex();
        }

        ~TaskList(){
            /* Hopefully no one is using this task list at this point. What
             * a fun potential race! Good job c++!
             */
            al_destroy_mutex(mutex);
            for (vector<Task*>::iterator it = tasks.begin(); it != tasks.end(); it++){
                delete *it;
            }
        }

        /* Pull the first task off the list. Whoever gets the task must
         * take care to delete it.
         */
        Task * getTask(){
            Task * out = NULL;
            al_lock_mutex(mutex);
            if (tasks.size() > 0){
                out = tasks[0];
                tasks.erase(tasks.begin());
            }
            al_unlock_mutex(mutex);
            return out;
        }

        /* Adds a task to the front of the work queue */
        void addTask(Task * task){
            al_lock_mutex(mutex);
            /* We actually don't care about old tasks so just erase them */
            for (vector<Task*>::iterator it = tasks.begin(); it != tasks.end(); it++){
                delete *it;
            }
            tasks.clear();
            tasks.insert(tasks.begin(), task);
            al_unlock_mutex(mutex);
        }

        ALLEGRO_MUTEX * mutex;
        vector<Task*> tasks;
    };

    /* Loads threads in the background */
    class Worker{
    public:
        Worker(TaskList & tasks):
        isAlive(true),
        tasks(tasks){
            aliveMutex = al_create_mutex();
            thread = NULL;
        }

        ~Worker(){
            kill();
            al_join_thread(thread, NULL);
            al_destroy_mutex(aliveMutex);
        }

        void start(){
            thread = al_create_thread(run, this);
            al_start_thread(thread);
        }

        bool isAlive;
        ALLEGRO_MUTEX * aliveMutex;
        ALLEGRO_THREAD * thread;
        TaskList & tasks;

        void load(Mailbox * box){
            al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
            ALLEGRO_BITMAP * out = al_load_bitmap(box->getFile().c_str());
            box->setBitmap(out);
        }

        /* Polls for a task */
        Task * nextTask(){
            while (alive()){
                Task * next = tasks.getTask();
                if (next != NULL){
                    return next;
                }
                al_rest(0.001);
            }
            return NULL;
        }

        bool alive(){
            bool out = false;
            al_lock_mutex(aliveMutex);
            out = isAlive;
            al_unlock_mutex(aliveMutex);
            return out;
        }

        void kill(){
            al_lock_mutex(aliveMutex);
            isAlive = false;
            al_unlock_mutex(aliveMutex);
        }

        void work(){
            while (alive()){
                /* nextMailbox will sleep until theres something ready */
                Task * next = nextTask();
                /* We might have died while waiting for a task */
                if (alive()){
                    load(next->getBox());
                }

                /* We are done with the task */
                delete next;
            }
        }

        static void * run(ALLEGRO_THREAD * thread, void * self){
            Worker * worker = (Worker*) self;
            worker->work();
            return NULL;
        }
    };

    ImageManager(ALLEGRO_EVENT_SOURCE * events):
    currentBitmap(NULL),
    events(events){
        for (int i = 0; i < MAX_WORKERS; i++){
            Worker * worker = new Worker(tasks);
            worker->start();
            workers.push_back(worker);
        }
    }

    ~ImageManager(){
        /* We kill all the workers so in theory there should be no one using
         * the task list when its destructor runs.
         */
        for (vector<Worker*>::iterator it = workers.begin(); it != workers.end(); it++){
            Worker * worker = *it;
            delete worker;
        }

        /* There is an interesting race here. The task list class will delete all
         * existing tasks but they are referencing mailboxes. We can't delete the
         * mailboxes first because the Task destructor will call box->dec(). We need
         * to somehow delete the mailboxes after the task list destructor runs.
         * I suppose the task list could be made a pointer so we can manually
         * schedule its deletion and then delete all the remaining mailboxes afterwards.
         *
         * In reality the number of tasks/mailboxes will be pretty small since workers
         * only hold onto 1 task at a time and the task list really only contains
         * 1 task list in its queue. So 3 tasks/mailboxes at most..
         */

        if (currentBitmap != NULL){
            al_destroy_bitmap(currentBitmap);
        }
    }

    /* Delete any mailboxes that are complete but dont match the current file */
    void cleanOldMailboxes(const string & filename){
        for (vector<Mailbox*>::iterator it = mailboxes.begin(); it != mailboxes.end(); /**/){
            Mailbox * box = *it;

            /* Delete the mailbox if it uses a file we dont care about
             * and its not reference by a task either because its completed
             * or because the task was removed from the queue before it
             * was started.
             *
             * The mailbox might be reference by a task currently being processed
             * by a worker and so the count will be non-zero.
             */
            if (box->getFile() != filename &&
                box->getCount() == 0){

                /* The bitmap in the mailbox may not have been loaded or a
                 * load was attempted but failed so the bitmap remains NULL.
                 */
                ALLEGRO_BITMAP * bitmap = box->getBitmap();
                if (bitmap != NULL){
                    al_destroy_bitmap(bitmap);
                }
                delete box;
                it = mailboxes.erase(it);
            } else {
                it++;
            }
        }
    }

    ALLEGRO_BITMAP * get(const string & filename){
        if (filename == currentFile && currentBitmap != NULL){
            return currentBitmap;
        }

        /* Its a new file so clear the old state */
        currentFile = filename;
        if (currentBitmap != NULL){
            al_destroy_bitmap(currentBitmap);
            currentBitmap = NULL;
        }

        cleanOldMailboxes(filename);

        /* Search for the mailbox that contains the given filename and check
         * if its done loading the bitmap.
         */
        for (vector<Mailbox*>::iterator it = mailboxes.begin(); it != mailboxes.end(); it++){
            Mailbox * box = *it;
            if (box->getFile() == filename){
                ALLEGRO_BITMAP * use = box->getBitmap();
                if (use != NULL){
                    /* We found a mailbox that was done loading so
                     * convert the loaded bitmap to a video one
                     * and destroy the mailbox.
                     */
                    if (currentBitmap != NULL){
                        al_destroy_bitmap(currentBitmap);
                    }

                    currentBitmap = use;
                    /* Convert it from memory to video */
                    al_convert_bitmap(use);
                    delete box;
                    mailboxes.erase(it);
                    return currentBitmap;
                } else {
                    /* Otherwise theres already a box for this bitmap so we
                     * must be waiting for it to complete.
                     */
                    return NULL;
                }
            }
        }

        /* No matching mailboxes so make a new one and add it to the task list */
        Mailbox * box = new Mailbox(filename, events);
        mailboxes.push_back(box);

        tasks.addTask(new Task(box));

        return NULL;
    }

    vector<Worker*> workers;
    vector<Mailbox*> mailboxes;
    TaskList tasks;

    string currentFile;
    ALLEGRO_BITMAP * currentBitmap;
    ALLEGRO_EVENT_SOURCE * events;
};

class View{
public:
    View(ALLEGRO_EVENT_SOURCE * events):
    thumbnailWidth(40),
    thumbnailHeight(40),
    thumbnailWidthSpace(4),
    thumbnailHeightSpace(4),
    show(0),
    scroll(0),
    percent(0),
    manager(events){
    }

    ~View(){
        for (vector<Image*>::iterator it = images.begin(); it != images.end(); it++){
            Image * image = *it;
            if (image->thumbnail != NULL){
                al_destroy_bitmap(image->thumbnail);
            }
            if (image->video != NULL){
                al_destroy_bitmap(image->video);
            }

            delete image;
        }
    }

    int maxThumbnails(ALLEGRO_DISPLAY * display) const {
        int top = al_get_display_height(display) / 3;
        int height = al_get_display_height(display) - top;
        return (int) ((height - thumbnailHeightSpace) / (thumbnailHeight + thumbnailHeightSpace)) *
            (int) ((al_get_display_width(display) - thumbnailWidthSpace) / (thumbnailWidth + thumbnailWidthSpace));
        // return ((al_get_display_width(display) - thumbnailWidthSpace) * height) / ((thumbnailWidth + thumbnailWidthSpace) * (thumbnailHeight + thumbnailHeightSpace));
    }

    int thumbnailsLine(ALLEGRO_DISPLAY * display) const {
        return (al_get_display_width(display) - thumbnailWidthSpace) / (thumbnailWidthSpace + thumbnailWidth);
    }

    void largerThumbnails(ALLEGRO_DISPLAY * display){
        thumbnailWidth += 5;
        thumbnailHeight += 5;
        updateScroll(display);
    }

    void smallerThumbnails(ALLEGRO_DISPLAY * display){
        thumbnailWidth -= 5;
        thumbnailHeight -= 5;
        if (thumbnailWidth < 5){
            thumbnailWidth = 5;
        }
        if (thumbnailHeight < 5){
            thumbnailHeight = 5;
        }

        updateScroll(display);
    }

    void move(ALLEGRO_DISPLAY * display, int much){
        if (images.size() > 0){
            show += much;
            if (show < 0){
                show = 0;
            }
            if (show >= images.size()){
                show = images.size() - 1;
            }
        }

        updateScroll(display);
    }

    void moveLeft(ALLEGRO_DISPLAY * display){
        move(display, -1);
    }
    
    void moveRight(ALLEGRO_DISPLAY * display){
        move(display, 1);
    }

    void moveDown(ALLEGRO_DISPLAY * display){
        move(display, thumbnailsLine(display));
    }

    void moveUp(ALLEGRO_DISPLAY * display){
        move(display, -thumbnailsLine(display));
    }
    
    void pageUp(ALLEGRO_DISPLAY * display){
        move(display, -maxThumbnails(display));
    }
    
    void pageDown(ALLEGRO_DISPLAY * display){
        move(display, maxThumbnails(display));
    }

    void updateScroll(ALLEGRO_DISPLAY * display){
        while (show < scroll){
            scroll -= thumbnailsLine(display);
            if (scroll < 0){
                scroll = 0;
            }
        }

        if (show > scroll){
            while (true){
                int many = show - scroll;
                if (many >= maxThumbnails(display) - thumbnailsLine(display)){
                    scroll += thumbnailsLine(display);
                } else {
                    break;
                }
            }
        }

        /*
        if (view.scroll < view.show - view.maxThumbnails(display) + view.thumbnailsLine(display)){
            view.scroll = view.show - view.maxThumbnails(display) + view.thumbnailsLine(display);
            if (view.scroll < 0){
                view.scroll = 0;
            }
        }
        */
    }
    
    ALLEGRO_BITMAP * getCurrentBitmap(){
        Image * current = currentImage();
        if (current != NULL){
            return manager.get(current->filename);
        }

        return NULL;
    }

    string getCurrentFilename() const {
        Image * current = currentImage();
        if (current != NULL){
            return current->filename;
        }
        return "unknown";
    }

    /* set all bitmaps that aren't being shown to memory and set the bitmaps
     * that are visible to video
     */
    void updateBitmaps(ALLEGRO_DISPLAY * display) const {
        /*
        al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
        for (int i = 0; i < scroll; i++){
            Image * image = images[i];
            ALLEGRO_BITMAP * bitmap = image->thumbnail;
            if ((al_get_bitmap_flags(bitmap) & ALLEGRO_VIDEO_BITMAP) != 0){
                al_convert_bitmap(bitmap);
            }
        }
        for (int i = scroll + maxThumbnails(display); i < images.size(); i++){
            Image * image = images[i];
            ALLEGRO_BITMAP * bitmap = image->thumbnail;
            if ((al_get_bitmap_flags(bitmap) & ALLEGRO_VIDEO_BITMAP) != 0){
                al_convert_bitmap(bitmap);
            }
        }
        */

        for (int i = 0; i < scroll; i++){
            Image * image = images[i];
            if (image->video != NULL){
                al_destroy_bitmap(image->video);
                image->video = NULL;
            }
        }
        for (int i = scroll + maxThumbnails(display); i < images.size(); i++){
            Image * image = images[i];
            if (image->video != NULL){
                al_destroy_bitmap(image->video);
                image->video = NULL;
            }
        }

        /* Set the visible ones to video */
        al_set_new_bitmap_flags(ALLEGRO_CONVERT_BITMAP);
        for (int i = scroll; i < scroll + maxThumbnails(display) && i < images.size(); i++){
            Image * image = images[i];
            if (image->video == NULL){
                image->video = al_clone_bitmap(image->thumbnail);
            }
        }

        /* Reset the default */
        al_set_new_bitmap_flags(ALLEGRO_CONVERT_BITMAP);

        /* Just to make sure the number of video bitmaps is the right amount */
        /*
        int totalVideo = 0;
        for (vector<Image*>::const_iterator it = images.begin(); it != images.end(); it++){
            ALLEGRO_BITMAP * bitmap = (*it)->image;
            if ((al_get_bitmap_flags(bitmap) & ALLEGRO_VIDEO_BITMAP) != 0){
                totalVideo += 1;
            }
        }

        printf("Total video bitmaps %d\n", totalVideo);
        */
    }

    Image * currentImage() const {
        if (show < images.size()){
            return images[show];
        }
        return NULL;
    }

    int thumbnailWidth;
    int thumbnailHeight;
    int thumbnailWidthSpace;
    int thumbnailHeightSpace;

    int show;
    int scroll;

    /* percent of files searched */
    int percent;

    vector<Image*> images;

    ImageManager manager;
};

static ALLEGRO_BITMAP * create_thumbnail(ALLEGRO_BITMAP * image){
    double scale = 1;

    /* Create thumbnails at 80x80. This is larger than the default
     * thumbnail size that the user will see so it gives them a chance
     * to increase the thumbnail size without messing up the images too much.
     * Once the thumbnail size is increased beyond 80x80 (with +/-) it will
     * start to look blocky.
     */
    double scaleWidth = 80.0 / al_get_bitmap_width(image);
    double scaleHeight = 80.0 / al_get_bitmap_height(image);

    if (scaleHeight < scaleWidth){
        scale = scaleHeight;
    } else {
        scale = scaleWidth;
    }
    ALLEGRO_BITMAP * thumbnail = al_create_bitmap(al_get_bitmap_width(image) * scale, al_get_bitmap_height(image) * scale);
    al_set_target_bitmap(thumbnail);
    al_clear_to_color(al_map_rgba_f(0, 0, 0, 0));
    al_draw_scaled_bitmap(image,
                          0, 0,
                          al_get_bitmap_width(image),
                          al_get_bitmap_height(image),
                          0, 0,
                          al_get_bitmap_width(thumbnail),
                          al_get_bitmap_height(thumbnail),
                          0);
    return thumbnail;
}

static void loadFiles(const vector<string> & files, ALLEGRO_EVENT_SOURCE * events){
    double percent = 0;
    al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
    int count = 0;
    for (vector<string>::const_iterator it = files.begin(); it != files.end(); it++, count++){
        al_lock_mutex(globalQuit);
        if (doQuit){
            al_unlock_mutex(globalQuit);
            break;
        }
        al_unlock_mutex(globalQuit);

        double now = (double)count / (double) files.size() * 100;
        if (now - percent >= 1){
            ALLEGRO_EVENT event;
            event.user.type = PERCENT_TYPE;
            event.user.data1 = (intptr_t) (int)now;
            al_emit_user_event(events, &event, NULL);
            percent = now;
        }

        ALLEGRO_BITMAP * image = al_load_bitmap(it->c_str());
        if (image != NULL){
            ALLEGRO_EVENT event;
            event.user.type = VIEW_TYPE;
            debug(" ..image %p\n", image);
            ALLEGRO_BITMAP * thumbnail = create_thumbnail(image);
            al_destroy_bitmap(image);
            Image * store = new Image(thumbnail, *it);
            event.user.data1 = (intptr_t) store;
            al_emit_user_event(events, &event, NULL);
        }
    }

    /* Output 100% at the end */
    {
        ALLEGRO_EVENT event;
        event.user.type = PERCENT_TYPE;
        event.user.data1 = (intptr_t) 100;
        al_emit_user_event(events, &event, NULL);
    }
}

struct LoadImagesStuff{
    ALLEGRO_EVENT_SOURCE * events;
    bool recursive;
};

vector<string> getFiles(bool recursive, ALLEGRO_FS_ENTRY * here){
    al_open_directory(here);
    ALLEGRO_FS_ENTRY * file = al_read_directory(here);
    vector<string> files;
    while (file != NULL){
        al_lock_mutex(globalQuit);
        if (doQuit){
            al_unlock_mutex(globalQuit);
            break;
        }
        al_unlock_mutex(globalQuit);

        debug("Entry %s\n", al_get_fs_entry_name(file));
        bool directory = al_get_fs_entry_mode(file) & ALLEGRO_FILEMODE_ISDIR;
        if (directory && recursive){
            vector<string> more = getFiles(recursive, file);
            files.insert(files.end(), more.begin(), more.end());
        } else {
            files.push_back(al_get_fs_entry_name(file));
        }
        al_destroy_fs_entry(file);
        file = al_read_directory(here);
    }
    al_close_directory(here);

    return files;
}

void * loadImages(ALLEGRO_THREAD * self, void * data){
    LoadImagesStuff * stuff = (LoadImagesStuff*) data;
    ALLEGRO_EVENT_SOURCE * events = stuff->events;
    bool recursive = stuff->recursive;

    ALLEGRO_FS_ENTRY * here = al_create_fs_entry(".");
    vector<string> files = getFiles(recursive, here);
    al_destroy_fs_entry(here);

    std::sort(files.begin(), files.end());
    loadFiles(files, events);

    return NULL;
}

static void redraw(ALLEGRO_DISPLAY * display, ALLEGRO_FONT * font, View & view){
    al_clear_to_color(al_map_rgb(0, 0, 0));

    double top = al_get_display_height(display) / 3.0;
    al_draw_line(0, top, al_get_display_width(display), top, al_map_rgb_f(1, 1, 1), 1);

    view.updateBitmaps(display);

    if (view.images.size() > view.show && view.getCurrentBitmap() != NULL){
        ALLEGRO_BITMAP * image = view.getCurrentBitmap();
        std::ostringstream number;
        number << "Image " << (view.show + 1) << " / " << view.images.size();
        al_draw_text(font, al_map_rgb_f(1, 1, 1), 1, 1, ALLEGRO_ALIGN_LEFT, number.str().c_str());
        number.str("");
        number << al_get_bitmap_width(image) << " x " << al_get_bitmap_height(image);
        al_draw_text(font, al_map_rgb_f(1, 1, 1), 1, 1 + al_get_font_line_height(font) + 1, ALLEGRO_ALIGN_LEFT, number.str().c_str());
        // double widthRatio = (double) al_get_display_width(display) / al_get_bitmap_width(image->image);
        // double heightRatio = (double) al_get_display_height(display) / al_get_bitmap_height(image->image);
        
        int px = al_get_display_width(display) / 2 - al_get_bitmap_width(image) / 2;
        int py = top / 2 - al_get_bitmap_height(image) / 2;
        int pw = al_get_bitmap_width(image);
        int ph = al_get_bitmap_height(image);

        double expandHeight = (top - al_get_font_line_height(font) - 10) / (double) al_get_bitmap_height(image);
        double expandWidth = (al_get_display_width(display) - 10) / (double) al_get_bitmap_width(image);

        double expand = 1;
        if (expandHeight < expandWidth){
            expand = expandHeight;
        } else {
            expand = expandWidth;
        }
        int newWidth = al_get_bitmap_width(image) * expand;
        int newHeight = al_get_bitmap_height(image) * expand;

        px = al_get_display_width(display) / 2 - newWidth / 2;
        py = (top - al_get_font_line_height(font)) / 2 - newHeight / 2;
        pw = newWidth;
        ph = newHeight;

        al_draw_scaled_bitmap(image, 0, 0, al_get_bitmap_width(image), al_get_bitmap_height(image),
                              px, py, pw, ph, 0);

        al_draw_text(font, al_map_rgb_f(1, 1, 1), al_get_display_width(display) / 2, top - al_get_font_line_height(font) - 1, ALLEGRO_ALIGN_CENTRE, view.getCurrentFilename().c_str());
    }

    if (view.percent < 100){
        std::ostringstream number;
        number << "Searching " << view.percent << "%";
        al_draw_text(font, al_map_rgb_f(1, 1, 1), al_get_display_width(display) - 1, 1, ALLEGRO_ALIGN_RIGHT, number.str().c_str());
    }

    int x = view.thumbnailWidthSpace;
    int y = top + view.thumbnailHeightSpace;

    int count = view.scroll;
    int total = view.maxThumbnails(display);
    int shown = 0;
    for (vector<Image*>::const_iterator it = view.images.begin() + view.scroll; it != view.images.end() && shown < total; it++, count++, shown++){
        Image * store = *it;
        ALLEGRO_BITMAP * image = store->video;

        if (image == NULL){
            printf("Video thumbnail image should not be null!\n");
            throw std::exception();
        }

        int px = x;
        int py = y;
        int pw = al_get_bitmap_width(image);
        int ph = al_get_bitmap_height(image);

        double expandHeight = (double) view.thumbnailHeight / al_get_bitmap_height(image);
        double expandWidth = (double) view.thumbnailWidth / al_get_bitmap_width(image);

        double expand = 1;
        if (expandHeight < expandWidth){
            expand = expandHeight;
        } else {
            expand = expandWidth;
        }
        int newWidth = al_get_bitmap_width(image) * expand;
        int newHeight = al_get_bitmap_height(image) * expand;

        px = x;
        py = y;
        pw = newWidth;
        ph = newHeight;

        debug("thumbnail at %d, %d %d, %d\n", px, py, pw, ph);
        al_draw_scaled_bitmap(image, 0, 0, al_get_bitmap_width(image), al_get_bitmap_height(image),
                              px, py, pw, ph, 0);

        if (count == view.show){
            al_draw_rectangle(px - 2, py - 2, px + pw + 2, py + ph + 2, al_map_rgb_f(1, 0, 0), 2);
        }

        x += view.thumbnailWidth + view.thumbnailWidthSpace;
        if (x + view.thumbnailWidth >= al_get_display_width(display)){
            x = 1;
            y += view.thumbnailHeight + view.thumbnailHeightSpace;
        }

        if (y + view.thumbnailHeight >= al_get_display_height(display)){
            debug("break height\n");
            break;
        }
    }
}

/* Get the font from the directory where the executable lives */
ALLEGRO_FONT * getFont(){
    std::ostringstream out;
    ALLEGRO_PATH * path = al_get_standard_path(ALLEGRO_EXENAME_PATH);
    debug("Original path is '%s'\n", al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP));
    al_set_path_filename(path, NULL);
    // al_remove_path_component(path, -1);
    out << al_path_cstr(path, ALLEGRO_NATIVE_PATH_SEP) << "/" << "arial.ttf";
    al_destroy_path(path);
    debug("Path is %s\n", out.str().c_str());
    return al_load_font(out.str().c_str(), 20, 0);
}

struct Position{
    int startX1, startY1;
    int startX2, startY2;
    int endX1, endY1;
    int endX2, endY2;
};

Position computePosition(ALLEGRO_DISPLAY * display, ALLEGRO_FONT * font, ALLEGRO_BITMAP * image){
    Position position;

    int pw = al_get_bitmap_width(image);
    int ph = al_get_bitmap_height(image);

    double top = al_get_display_height(display) / 3.0;

    double expandHeight = (top - al_get_font_line_height(font) - 10) / (double) al_get_bitmap_height(image);
    double expandWidth = (al_get_display_width(display) - 10) / (double) al_get_bitmap_width(image);

    double expand = 1;
    if (expandHeight < expandWidth){
        expand = expandHeight;
    } else {
        expand = expandWidth;
    }
    int newWidth = al_get_bitmap_width(image) * expand;
    int newHeight = al_get_bitmap_height(image) * expand;

    int px = al_get_display_width(display) / 2 - newWidth / 2;
    int py = (top - al_get_font_line_height(font)) / 2 - newHeight / 2;
    pw = newWidth;
    ph = newHeight;

    position.startX1 = px;
    position.startY1 = py;
    position.startX2 = px + pw;
    position.startY2 = py + ph;

    expandWidth = (double) (al_get_display_width(display) - 10) / al_get_bitmap_width(image);
    expandHeight = (double) (al_get_display_height(display) - 10) / al_get_bitmap_height(image);

    newWidth = al_get_bitmap_width(image);
    newHeight = al_get_bitmap_height(image);
    if (expandWidth < 1 || expandHeight < 1){
        double expand = 1;
        if (expandHeight < expandWidth){
            expand = expandHeight;
        } else {
            expand = expandWidth;
        }
        newWidth = al_get_bitmap_width(image) * expand;
        newHeight = al_get_bitmap_height(image) * expand;
    }

    position.endX1 = al_get_display_width(display) / 2 - newWidth / 2;
    position.endY1 = al_get_display_height(display) / 2 - newHeight / 2;
    position.endX2 = al_get_display_width(display) / 2 + newWidth / 2;
    position.endY2 = al_get_display_height(display) / 2 + newHeight / 2;

    return position;
}
                                    
void drawCenter(ALLEGRO_DISPLAY * display, ALLEGRO_BITMAP * image, const Position & position, int steps, int much){

    /* Darken rest of the screen */
    al_set_blender(ALLEGRO_ADD, ALLEGRO_ALPHA, ALLEGRO_INVERSE_ALPHA);
    al_draw_filled_rectangle(0, 0, al_get_display_width(display), al_get_display_height(display), al_map_rgba_f(0, 0, 0, 0.8));
    al_set_blender(ALLEGRO_ADD, ALLEGRO_ONE, ALLEGRO_ZERO);

    // double interpolate = (double) much / (double) steps;
    double interpolate = sin((double) much * 90.0 / (double) steps * 3.14159 / 180);
    int px = (int)(position.startX1 * (1 - interpolate) + position.endX1 * interpolate);
    int pw = (int)(position.startX2 * (1 - interpolate) + position.endX2 * interpolate - px);
    int py = (int)(position.startY1 * (1 - interpolate) + position.endY1 * interpolate);
    int ph = (int)(position.startY2 * (1 - interpolate) + position.endY2 * interpolate - py);
    al_draw_scaled_bitmap(image, 0, 0, al_get_bitmap_width(image), al_get_bitmap_height(image),
                          px, py, pw, ph, 0);

}

int main(int argc, char ** argv){
    al_init();
    al_install_keyboard();
    al_init_image_addon();
    al_init_primitives_addon();
    al_init_font_addon();
    al_init_ttf_addon();
    al_set_new_display_flags(ALLEGRO_RESIZABLE | ALLEGRO_GENERATE_EXPOSE_EVENTS);
    ALLEGRO_DISPLAY * display = al_create_display(800, 700);
    ALLEGRO_EVENT_QUEUE * queue = al_create_event_queue();
    al_register_event_source(queue, al_get_keyboard_event_source());
    ALLEGRO_EVENT_SOURCE imageSource;
    al_init_user_event_source(&imageSource);
    al_register_event_source(queue, &imageSource);
    al_register_event_source(queue, al_get_display_event_source(display));

    globalQuit = al_create_mutex();

    ALLEGRO_FONT * font = getFont();
    if (font == NULL){
        printf("Could not load font\n");
        al_destroy_mutex(globalQuit);
        return -1;
    }

    View view(&imageSource);

    debug("thumbs %d\n", view.maxThumbnails(display));

    redraw(display, font, view);
    al_flip_display();

    /* Ok to put on the stack since we are in main */
    LoadImagesStuff stuff;
    stuff.events = &imageSource;
    stuff.recursive = false;
    if (argc > 1 && (string(argv[1]) == "-r" ||
                     string(argv[1]) == "-R")){
        stuff.recursive = true;
    }
    ALLEGRO_THREAD * imageThread = al_create_thread(loadImages, &stuff);
    al_start_thread(imageThread);

    ALLEGRO_EVENT event;
    while (true){
        bool draw = false;
        do{
            al_wait_for_event(queue, &event);
            if (event.type == ALLEGRO_EVENT_KEY_CHAR){
                switch (event.keyboard.keycode){
                    case ALLEGRO_KEY_ESCAPE: {
                        /* BOOYA! This label can be jumped to by the other loop */
                        quit_program:

                        al_lock_mutex(globalQuit);
                        doQuit = true;
                        al_unlock_mutex(globalQuit);
                        al_join_thread(imageThread, NULL);
                        al_destroy_user_event_source(&imageSource);
                        al_destroy_display(display);
                        debug("Quit\n");
                        return 0;
                    }
                    case ALLEGRO_KEY_LEFT: {
                        draw = true;
                        view.moveLeft(display);
                        break;
                    }
                    case ALLEGRO_KEY_RIGHT: {
                        draw = true;
                        view.moveRight(display);
                        break;
                    }
                    case ALLEGRO_KEY_DOWN: {
                        draw = true;
                        view.moveDown(display);
                        break;
                    }
                    case ALLEGRO_KEY_UP: {
                        draw = true;
                        view.moveUp(display);
                        break;
                    }
                    case ALLEGRO_KEY_PGDN: {
                        draw = true;
                        view.pageDown(display);
                        break;
                    }
                    case ALLEGRO_KEY_PGUP: {
                        draw = true;
                        view.pageUp(display);
                        break;
                    }
                    
                    case ALLEGRO_KEY_ENTER: {

                        /* Start a new loop that shows an animation of the current
                         * image being interpolated to its position at the center
                         * of the screen.
                         */

                        Image * image = view.currentImage();
                        if (image != NULL){
                            bool ok = true;
                            bool wait = true;

                            /* Wait for the main image to be loaded.
                             *
                             * FIXME: there is a non-zero chance the bitmap
                             * can't be loaded so it will always be NULL. Instead
                             * we can wait for a LOAD_TYPE event and if the bitmap
                             * is still NULL after that then we should fail to show
                             * it in the center.
                             */
                            while (view.getCurrentBitmap() == NULL){
                                al_rest(0.001);
                            }

                            ALLEGRO_BITMAP * bitmap = view.getCurrentBitmap();

                            ALLEGRO_TIMER * timer = al_create_timer(0.02);
                            al_start_timer(timer);
                            al_register_event_source(queue, al_get_timer_event_source(timer));
                            Position position = computePosition(display, font, bitmap);
                            const int steps = 12;
                            int much = 0;
                            while (ok){
                                bool draw = false;
                                al_wait_for_event(queue, &event);
                                if (event.type == ALLEGRO_EVENT_KEY_CHAR){
                                    switch (event.keyboard.keycode){
                                        case ALLEGRO_KEY_ESCAPE: {
                                            goto quit_program;
                                            break;
                                        }
                                        case ALLEGRO_KEY_ENTER: {
                                            ok = false;
                                            wait = false;
                                            break;
                                        }
                                    }
                                } else if (event.type == ALLEGRO_EVENT_DISPLAY_EXPOSE){
                                    draw = true;
                                } else if (event.type == ALLEGRO_EVENT_DISPLAY_RESIZE){
                                    al_acknowledge_resize(event.display.source);
                                    position = computePosition(display, font, bitmap);
                                    draw = true;
                                } else if (event.type == ALLEGRO_EVENT_TIMER){
                                    much += 1;
                                    if (much == steps){
                                        ok = false;
                                    }
                                    draw = true;
                                }

                                if (draw){
                                    redraw(display, font, view);
                                    drawCenter(display, bitmap, position, steps, much);
                                    al_flip_display();
                                }
                            }

                            if (wait){
                                /* Wait for key press */
                                ok = true;
                                while (ok){
                                    al_wait_for_event(queue, &event);
                                    bool draw = false;
                                    if (event.type == ALLEGRO_EVENT_KEY_CHAR){
                                        switch (event.keyboard.keycode){
                                            case ALLEGRO_KEY_ESCAPE: {
                                                goto quit_program;
                                                break;
                                            }
                                            case ALLEGRO_KEY_ENTER: {
                                                ok = false;
                                                break;
                                            }
                                        }
                                    } else if (event.type == ALLEGRO_EVENT_DISPLAY_EXPOSE){
                                        draw = true;
                                    } else if (event.type == ALLEGRO_EVENT_DISPLAY_RESIZE){
                                        al_acknowledge_resize(event.display.source);
                                        position = computePosition(display, font, bitmap);
                                        draw = true;
                                    }

                                    if (draw){
                                        redraw(display, font, view);
                                        drawCenter(display, bitmap, position, steps, much);
                                        al_flip_display();
                                    }
                                }
                            }

                            /* Uninterpolate the image */
                            ok = true;
                            while (ok){
                                al_wait_for_event(queue, &event);
                                bool draw = false;
                                if (event.type == ALLEGRO_EVENT_KEY_CHAR){
                                    switch (event.keyboard.keycode){
                                        case ALLEGRO_KEY_ESCAPE: {
                                            goto quit_program;
                                            break;
                                        }
                                        case ALLEGRO_KEY_ENTER: {
                                            ok = false;
                                            break;
                                        }
                                    }
                                } else if (event.type == ALLEGRO_EVENT_DISPLAY_EXPOSE){
                                    draw = true;
                                } else if (event.type == ALLEGRO_EVENT_TIMER){
                                    much -= 1;
                                    if (much == 0){
                                        ok = false;
                                    }
                                    draw = true;
                                } else if (event.type == ALLEGRO_EVENT_DISPLAY_RESIZE){
                                    al_acknowledge_resize(event.display.source);
                                    position = computePosition(display, font, bitmap);
                                    draw = true;
                                }

                                if (draw){
                                    redraw(display, font, view);
                                    drawCenter(display, bitmap, position, steps, much);
                                    al_flip_display();
                                }
                            }

                            redraw(display, font, view);
                            al_flip_display();

                            al_stop_timer(timer);
                            al_destroy_timer(timer);
                        }

                        break;
                    }
                    default: {
                    }
                }
                switch (event.keyboard.unichar){
                    case '-': {
                        view.smallerThumbnails(display);
                        draw = true;
                        break;
                    }
                    case '=': {
                        view.largerThumbnails(display);
                        draw = true;
                        break;
                    }
                }
            } else if (event.type == VIEW_TYPE){
                debug("Got image %p\n", event.user.data1);
                Image * image = (Image*) event.user.data1;
                view.images.push_back(image);
                draw = true;
            } else if (event.type == PERCENT_TYPE){
                int percent = (int) event.user.data1;
                view.percent = percent;
                draw = true;
            } else if (event.type == LOAD_TYPE){
                draw = true;
            } else if (event.type == ALLEGRO_EVENT_DISPLAY_RESIZE){
                al_acknowledge_resize(event.display.source);
                draw = true;
            } else if (event.type == ALLEGRO_EVENT_DISPLAY_EXPOSE){
                draw = true;
            }
        } while (al_peek_next_event(queue, &event));

        if (draw){
            redraw(display, font, view);
            al_flip_display();
        }
    }

    al_destroy_display(display);
}
