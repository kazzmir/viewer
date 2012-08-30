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

// #define debug(...) printf(__VA_ARGS__)
#define debug(...)

ALLEGRO_MUTEX * globalQuit;
bool doQuit = false;

struct Image{
    Image(ALLEGRO_BITMAP * thumbnail, const string & name):
        thumbnail(thumbnail),
        image(NULL),
        filename(name){
        }

    ALLEGRO_BITMAP * thumbnail;
    ALLEGRO_BITMAP * image;
    string filename;
};

static bool sortImage(Image * a, Image * b){
    return a->filename < b->filename;
}

class View{
public:
    View():
    thumbnailWidth(40),
    thumbnailHeight(40),
    thumbnailWidthSpace(4),
    thumbnailHeightSpace(4),
    show(0),
    scroll(0){
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
            if (images[show]->image != NULL){
                al_destroy_bitmap(images[show]->image);
                images[show]->image = NULL;
            }
            show += much;
            if (show < 0){
                show = 0;
            }
            if (show >= images.size()){
                show = images.size() - 1;
            }
            images[show]->image = al_load_bitmap(images[show]->filename.c_str());
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

    /* set all bitmaps that aren't being shown to memory and set the bitmaps
     * that are visible to video
     */
    void updateBitmaps(ALLEGRO_DISPLAY * display) const {
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

        /* Set the visible ones to video */
        al_set_new_bitmap_flags(ALLEGRO_CONVERT_BITMAP);
        for (int i = scroll; i < scroll + maxThumbnails(display) && i < images.size(); i++){
            Image * image = images[i];
            ALLEGRO_BITMAP * bitmap = image->thumbnail;
            if ((al_get_bitmap_flags(bitmap) & ALLEGRO_MEMORY_BITMAP) != 0){
                al_convert_bitmap(bitmap);
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
    vector<Image*> images;
};

void * loadImages(ALLEGRO_THREAD * self, void * data){
    ALLEGRO_EVENT_SOURCE * events = (ALLEGRO_EVENT_SOURCE*) data;
    ALLEGRO_FS_ENTRY * here = al_create_fs_entry(".");
    al_open_directory(here);
    ALLEGRO_FS_ENTRY * file = al_read_directory(here);
    al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
    vector<string> files;
    while (file != NULL){
        al_lock_mutex(globalQuit);
        if (doQuit){
            al_unlock_mutex(globalQuit);
            break;
        }
        al_unlock_mutex(globalQuit);

        debug("Entry %s\n", al_get_fs_entry_name(file));
        files.push_back(al_get_fs_entry_name(file));
        al_destroy_fs_entry(file);
        file = al_read_directory(here);
    }

    std::sort(files.begin(), files.end());

    for (vector<string>::iterator it = files.begin(); it != files.end(); it++){
        al_lock_mutex(globalQuit);
        if (doQuit){
            al_unlock_mutex(globalQuit);
            break;
        }
        al_unlock_mutex(globalQuit);

        ALLEGRO_BITMAP * image = al_load_bitmap(it->c_str());
        if (image != NULL){
            ALLEGRO_EVENT event;
            event.user.type = ALLEGRO_GET_EVENT_TYPE('V', 'I', 'E', 'W');
            double scale = 1;
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
            al_draw_scaled_bitmap(image, 0, 0, al_get_bitmap_width(image), al_get_bitmap_height(image), 0, 0, al_get_bitmap_width(thumbnail), al_get_bitmap_height(thumbnail), 0);
            al_destroy_bitmap(image);
            Image * store = new Image(thumbnail, *it);
            event.user.data1 = (intptr_t) store;
            al_emit_user_event(events, &event, NULL);
            debug(" ..image %p\n", image);
        }
    }

    al_close_directory(here);
    al_destroy_fs_entry(here);

    return NULL;
}

static void redraw(ALLEGRO_DISPLAY * display, ALLEGRO_FONT * font, const View & view){
    al_clear_to_color(al_map_rgb(0, 0, 0));

    double top = al_get_display_height(display) / 3.0;
    al_draw_line(0, top, al_get_display_width(display), top, al_map_rgb_f(1, 1, 1), 1);

    view.updateBitmaps(display);

    if (view.images.size() > view.show && view.images[view.show]->image != NULL){
        Image * image = view.images[view.show];
        std::ostringstream number;
        number << "Image " << (view.show + 1) << " / " << view.images.size();
        al_draw_text(font, al_map_rgb_f(1, 1, 1), 1, 1, ALLEGRO_ALIGN_LEFT, number.str().c_str());
        number.str("");
        number << al_get_bitmap_width(image->image) << " x " << al_get_bitmap_height(image->image);
        al_draw_text(font, al_map_rgb_f(1, 1, 1), 1, 1 + al_get_font_line_height(font) + 1, ALLEGRO_ALIGN_LEFT, number.str().c_str());
        // double widthRatio = (double) al_get_display_width(display) / al_get_bitmap_width(image->image);
        // double heightRatio = (double) al_get_display_height(display) / al_get_bitmap_height(image->image);
        
        int px = al_get_display_width(display) / 2 - al_get_bitmap_width(image->image) / 2;
        int py = top / 2 - al_get_bitmap_height(image->image) / 2;
        int pw = al_get_bitmap_width(image->image);
        int ph = al_get_bitmap_height(image->image);

        double expandHeight = (top - al_get_font_line_height(font) - 10) / (double) al_get_bitmap_height(image->image);
        double expandWidth = (al_get_display_width(display) - 10) / (double) al_get_bitmap_width(image->image);

        double expand = 1;
        if (expandHeight < expandWidth){
            expand = expandHeight;
        } else {
            expand = expandWidth;
        }
        int newWidth = al_get_bitmap_width(image->image) * expand;
        int newHeight = al_get_bitmap_height(image->image) * expand;

        px = al_get_display_width(display) / 2 - newWidth / 2;
        py = (top - al_get_font_line_height(font)) / 2 - newHeight / 2;
        pw = newWidth;
        ph = newHeight;

        al_draw_scaled_bitmap(image->image, 0, 0, al_get_bitmap_width(image->image), al_get_bitmap_height(image->image),
                              px, py, pw, ph, 0);

        al_draw_text(font, al_map_rgb_f(1, 1, 1), al_get_display_width(display) / 2, top - al_get_font_line_height(font) - 1, ALLEGRO_ALIGN_CENTRE, image->filename.c_str()); 
    }

    int x = view.thumbnailWidthSpace;
    int y = top + view.thumbnailHeightSpace;

    int count = view.scroll;
    for (vector<Image*>::const_iterator it = view.images.begin() + view.scroll; it != view.images.end(); it++, count++){
        Image * store = *it;
        ALLEGRO_BITMAP * image = store->thumbnail;

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
    al_remove_path_component(path, -1);
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

Position computePosition(ALLEGRO_DISPLAY * display, ALLEGRO_FONT * font, Image * image){
    Position position;

    int pw = al_get_bitmap_width(image->image);
    int ph = al_get_bitmap_height(image->image);

    double top = al_get_display_height(display) / 3.0;

    double expandHeight = (top - al_get_font_line_height(font) - 10) / (double) al_get_bitmap_height(image->image);
    double expandWidth = (al_get_display_width(display) - 10) / (double) al_get_bitmap_width(image->image);

    double expand = 1;
    if (expandHeight < expandWidth){
        expand = expandHeight;
    } else {
        expand = expandWidth;
    }
    int newWidth = al_get_bitmap_width(image->image) * expand;
    int newHeight = al_get_bitmap_height(image->image) * expand;

    int px = al_get_display_width(display) / 2 - newWidth / 2;
    int py = (top - al_get_font_line_height(font)) / 2 - newHeight / 2;
    pw = newWidth;
    ph = newHeight;

    position.startX1 = px;
    position.startY1 = py;
    position.startX2 = px + pw;
    position.startY2 = py + ph;

    expandWidth = (double) (al_get_display_width(display) - 10) / al_get_bitmap_width(image->image);
    expandHeight = (double) (al_get_display_height(display) - 10) / al_get_bitmap_height(image->image);

    newWidth = al_get_bitmap_width(image->image);
    newHeight = al_get_bitmap_height(image->image);
    if (expandWidth < 1 || expandHeight < 1){
        double expand = 1;
        if (expandHeight < expandWidth){
            expand = expandHeight;
        } else {
            expand = expandWidth;
        }
        newWidth = al_get_bitmap_width(image->image) * expand;
        newHeight = al_get_bitmap_height(image->image) * expand;
    }

    position.endX1 = al_get_display_width(display) / 2 - newWidth / 2;
    position.endY1 = al_get_display_height(display) / 2 - newHeight / 2;
    position.endX2 = al_get_display_width(display) / 2 + newWidth / 2;
    position.endY2 = al_get_display_height(display) / 2 + newHeight / 2;

    return position;
}
                                    
void drawCenter(ALLEGRO_DISPLAY * display, Image * image, const Position & position, int steps, int much){

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
    al_draw_scaled_bitmap(image->image, 0, 0, al_get_bitmap_width(image->image), al_get_bitmap_height(image->image),
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
        return -1;
    }

    View view;

    debug("thumbs %d\n", view.maxThumbnails(display));

    redraw(display, font, view);
    al_flip_display();

    ALLEGRO_THREAD * imageThread = al_create_thread(loadImages, &imageSource);
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
                    case ALLEGRO_KEY_MINUS: {
                        view.smallerThumbnails(display);
                        draw = true;
                        break;
                    }
                    case ALLEGRO_KEY_EQUALS: {
                        view.largerThumbnails(display);
                        draw = true;
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
                            ALLEGRO_TIMER * timer = al_create_timer(0.02);
                            al_start_timer(timer);
                            al_register_event_source(queue, al_get_timer_event_source(timer));
                            Position position = computePosition(display, font, image);
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
                                    position = computePosition(display, font, image);
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
                                    drawCenter(display, image, position, steps, much);
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
                                        position = computePosition(display, font, image);
                                        draw = true;
                                    }

                                    if (draw){
                                        redraw(display, font, view);
                                        drawCenter(display, image, position, steps, much);
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
                                    position = computePosition(display, font, image);
                                    draw = true;
                                }

                                if (draw){
                                    redraw(display, font, view);
                                    drawCenter(display, image, position, steps, much);
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
            } else if (event.type == ALLEGRO_GET_EVENT_TYPE('V', 'I', 'E', 'W')){
                debug("Got image %p\n", event.user.data1);
                Image * image = (Image*) event.user.data1;
                view.images.push_back(image);
                if (view.images[view.show]->image == NULL){
                    view.images[view.show]->image = al_load_bitmap(view.images[view.show]->filename.c_str());
                }
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
