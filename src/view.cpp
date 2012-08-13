#include <stdio.h>
#include <allegro5/allegro.h>
#include <allegro5/allegro_image.h>
#include <allegro5/allegro_primitives.h>
#include <allegro5/allegro_font.h>
#include <allegro5/allegro_ttf.h>
#include <vector>
#include <string>
#include <sstream>

using std::vector;
using std::string;

// #define debug(...) printf(__VA_ARGS__)
#define debug(...)

const int thumbnailWidth = 40;
const int thumbnailHeight = 40;
const int thumbnailWidthSpace = 4;
const int thumbnailHeightSpace = 4;

struct Image{
    Image(ALLEGRO_BITMAP * image, const char * name):
        image(image), filename(name){
        }

    ALLEGRO_BITMAP * image;
    string filename;
};

int maxThumbnails(ALLEGRO_DISPLAY * display){
    int top = al_get_display_height(display) / 3;
    int height = al_get_display_height(display) - top;
    return (int) ((height - thumbnailHeightSpace) / (thumbnailHeight + thumbnailHeightSpace)) *
           (int) ((al_get_display_width(display) - thumbnailWidthSpace) / (thumbnailWidth + thumbnailWidthSpace));
    // return ((al_get_display_width(display) - thumbnailWidthSpace) * height) / ((thumbnailWidth + thumbnailWidthSpace) * (thumbnailHeight + thumbnailHeightSpace));
}

int thumbnailsLine(ALLEGRO_DISPLAY * display){
    return (al_get_display_width(display) - thumbnailWidthSpace) / (thumbnailWidthSpace + thumbnailWidth);
}

void * loadImages(ALLEGRO_THREAD * self, void * data){
    ALLEGRO_EVENT_SOURCE * events = (ALLEGRO_EVENT_SOURCE*) data;
    ALLEGRO_FS_ENTRY * here = al_create_fs_entry(".");
    al_open_directory(here);
    ALLEGRO_FS_ENTRY * file = al_read_directory(here);
    al_set_new_bitmap_flags(ALLEGRO_MEMORY_BITMAP);
    while (file != NULL){
        debug("Entry %s\n", al_get_fs_entry_name(file));
        ALLEGRO_BITMAP * image = al_load_bitmap(al_get_fs_entry_name(file));
        if (image != NULL){
            ALLEGRO_EVENT event;
            event.user.type = ALLEGRO_GET_EVENT_TYPE('V', 'I', 'E', 'W');
            Image * store = new Image(image, al_get_fs_entry_name(file));
            event.user.data1 = (intptr_t) store;
            al_emit_user_event(events, &event, NULL);
            debug(" ..image %p\n", image);
        }
        file = al_read_directory(here);
    }
    al_close_directory(here);
    al_destroy_fs_entry(here);

    return NULL;
}

struct View{
    int show;
    int scroll;
    vector<Image*> images;
};

static void redraw(ALLEGRO_DISPLAY * display, ALLEGRO_FONT * font, const View & view){
    al_clear_to_color(al_map_rgb(0, 0, 0));

    double top = al_get_display_height(display) / 3.0;
    al_draw_line(0, top, al_get_display_width(display), top, al_map_rgb_f(1, 1, 1), 1);
    

    if (view.images.size() > 0){
        Image * image = view.images[view.show];
        double widthRatio = (double) al_get_display_width(display) / al_get_bitmap_width(image->image);
        double heightRatio = (double) al_get_display_height(display) / al_get_bitmap_height(image->image);
        
        int px = al_get_display_width(display) / 2 - al_get_bitmap_width(image->image) / 2;
        int py = top / 2 - al_get_bitmap_height(image->image) / 2;
        int pw = al_get_bitmap_width(image->image);
        int ph = al_get_bitmap_height(image->image);

        double expandHeight = (top - al_get_font_line_height(font) - 10) / al_get_bitmap_height(image->image);
        double expandWidth = (al_get_display_width(display) - 10) / al_get_bitmap_width(image->image);

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

    
    int x = thumbnailWidthSpace;
    int y = top + thumbnailHeightSpace;

    int count = view.scroll;
    for (vector<Image*>::const_iterator it = view.images.begin() + view.scroll; it != view.images.end(); it++, count++){
        Image * store = *it;
        ALLEGRO_BITMAP * image = store->image;

        int px = x;
        int py = y;
        int pw = al_get_bitmap_width(image);
        int ph = al_get_bitmap_height(image);

        double expandHeight = (double) thumbnailHeight / al_get_bitmap_height(image);
        double expandWidth = (double) thumbnailWidth / al_get_bitmap_width(image);

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

        x += thumbnailWidth + thumbnailWidthSpace;
        if (x + thumbnailWidth >= al_get_display_width(display)){
            x = 1;
            y += thumbnailHeight + thumbnailHeightSpace;
        }

        if (y + thumbnailHeight >= al_get_display_height(display)){
            debug("break height\n");
            break;
        }
    }

    al_flip_display();
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

int main(){
    al_init();
    al_install_keyboard();
    al_init_image_addon();
    al_init_primitives_addon();
    al_init_font_addon();
    al_init_ttf_addon();
    al_set_new_display_flags(ALLEGRO_RESIZABLE);
    ALLEGRO_DISPLAY * display = al_create_display(800, 700);
    ALLEGRO_EVENT_QUEUE * queue = al_create_event_queue();
    al_register_event_source(queue, al_get_keyboard_event_source());
    ALLEGRO_EVENT_SOURCE imageSource;
    al_init_user_event_source(&imageSource);
    al_register_event_source(queue, &imageSource);
    al_register_event_source(queue, al_get_display_event_source(display));

    ALLEGRO_FONT * font = getFont();
    if (font == NULL){
        printf("Could not load font\n");
        return -1;
    }

    debug("thumbs %d\n", maxThumbnails(display));

    ALLEGRO_THREAD * imageThread = al_create_thread(loadImages, &imageSource);
    al_start_thread(imageThread);

    View view;
    view.show = 0;
    view.scroll = 0;

    ALLEGRO_EVENT event;
    while (true){
        bool draw = false;
        do{
            al_wait_for_event(queue, &event);
            if (event.type == ALLEGRO_EVENT_KEY_CHAR){
                if (event.keyboard.keycode == ALLEGRO_KEY_ESCAPE){
                    al_destroy_user_event_source(&imageSource);
                    al_join_thread(imageThread, NULL);
                    al_destroy_display(display);
                    debug("Quit\n");
                    return 0;
                } else if (event.keyboard.keycode == ALLEGRO_KEY_LEFT){
                    draw = true;
                    if (view.images.size() > 0){
                        view.show = (view.show - 1 + view.images.size()) % view.images.size();
                    }
                } else if (event.keyboard.keycode == ALLEGRO_KEY_RIGHT){
                    draw = true;
                    if (view.images.size() > 0){
                        view.show = (view.show + 1 + view.images.size()) % view.images.size();
                    }
                } else if (event.keyboard.keycode == ALLEGRO_KEY_DOWN){
                    draw = true;
                    if (view.images.size() > 0){
                        view.show = (view.show + thumbnailsLine(display) + view.images.size()) % view.images.size();
                    }
                } else if (event.keyboard.keycode == ALLEGRO_KEY_UP){
                    draw = true;
                    if (view.images.size() > 0){
                        view.show = (view.show - thumbnailsLine(display) + view.images.size()) % view.images.size();
                    }
                }

                if (view.show < view.scroll){
                    view.scroll -= thumbnailsLine(display);
                    if (view.scroll < 0){
                        view.scroll = 0;
                    }
                }

                if (view.show > view.scroll){
                    while (true){
                        int many = view.show - view.scroll;
                        if (many >= maxThumbnails(display) - thumbnailsLine(display)){
                            view.scroll += thumbnailsLine(display);
                        } else {
                            break;
                        }
                    }
                }

                if (view.scroll < view.show - maxThumbnails(display) + thumbnailsLine(display)){
                    view.scroll = view.show - maxThumbnails(display) + thumbnailsLine(display);
                    if (view.scroll < 0){
                        view.scroll = 0;
                    }
                }

            } else if (event.type == ALLEGRO_GET_EVENT_TYPE('V', 'I', 'E', 'W')){
                debug("Got image %p\n", event.user.data1);
                Image * image = (Image*) event.user.data1;
                al_convert_bitmap(image->image);
                view.images.push_back(image);
                draw = true;
            } else if (event.type == ALLEGRO_EVENT_DISPLAY_RESIZE){
                al_acknowledge_resize(display);
                al_resize_display(display, event.display.width, event.display.height);
                draw = true;
            }
        } while (al_peek_next_event(queue, &event));
            
        if (draw){
            redraw(display, font, view);
        }
    }

    al_destroy_display(display);
}
