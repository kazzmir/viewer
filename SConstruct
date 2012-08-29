import os

env = Environment(ENV = os.environ)

source = Split("""view.cpp""")
env.VariantDir('build', 'src')
env.Append(CCFLAGS = ['-g3', '-Wall'])
env.ParseConfig('pkg-config allegro-5.1 allegro_main-5.1 allegro_font-5.1 allegro_ttf-5.1 allegro_primitives-5.1 allegro_image-5.1 --cflags --libs')
# env.ParseConfig('pkg-config allegro-debug-5.1 allegro_main-debug-5.1 allegro_font-debug-5.1 allegro_ttf-debug-5.1 allegro_primitives-debug-5.1 allegro_image-debug-5.1 --cflags --libs')
env.Program('viewer', ['build/%s' % file for file in source])
