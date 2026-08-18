/* atracker by Forrest England 2026 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <SDL/SDL.h>
#include <SDL/SDL_image.h>
#include <jack/jack.h>

jack_port_t* input_port;
jack_port_t* output_port;
jack_client_t* client;

int testtone_on = 0;
double phase = 0.0;
const double frequency = 440.0;
double testtone_amp = 0.1;

// oscilloscope buffer
#define OSC_BUFFER_SIZE 2048
float osc_buffer[OSC_BUFFER_SIZE];

const int screen_w = 640;
const int screen_h = 480;

int update = 1;
int quit = 0;

const int font_char_width = 16;
const int font_char_height = 16;

#define FPS 30
#define TICKS_PER_FRAME (1000 / FPS)

typedef enum {
  testtone_on_field,
  testtone_amp_field,
  compress_threshold_field,
  compress_makeup_field,
  compress_ratio_field,
  compress_attack_field,
  compress_release_field,
  field_count
} uifield;

uifield focusfield = testtone_on_field;

// compressor stuff
float threshold = 0.5;
float ratio = 4.0;
float attack = 0.01;
float release = 0.1;
float makeup = 1.0;
// compressor state
float envelope = 0.0;

int process(jack_nframes_t nframes, void* arg) {
  
  jack_default_audio_sample_t* in = (jack_default_audio_sample_t*)jack_port_get_buffer(input_port, nframes);
  jack_default_audio_sample_t* out = (jack_default_audio_sample_t*)jack_port_get_buffer(output_port, nframes);

  // get current sample rate from the jack client
  double sr = jack_get_sample_rate(client);

  // calculate phase increment based on frequency and sample rate
  double phase_increment = 2.0 * M_PI * frequency / sr;

  for (jack_nframes_t i=0; i<nframes; i++) {

    jack_default_audio_sample_t o = 0.0;

    if (testtone_on) {
      o += (jack_default_audio_sample_t)sin(phase) * testtone_amp;;
      phase += phase_increment;

      // wrap phase to prevent precision loss over long periods
      if (phase >= 2.0 * M_PI) {
	phase -= 2.0 * M_PI;
      }
    }

    jack_default_audio_sample_t inval = in[i];

    // apply compression
    float inamp = fabsf(inval);
    if (inamp > envelope) {
      envelope = attack * (envelope - inamp) + inamp;
    } else {
      envelope = release * (envelope - inamp) + inamp;
    }
    // gain calculation
    float cgain = 1.0;
    if (envelope > threshold) {
      cgain = threshold + (envelope - threshold) / ratio;
      cgain = cgain / envelope;
    }
    

    o += inval * cgain * makeup;

    out[i] = o;
  }

  // copy audio output to oscilloscope buffer
  size_t move_size = OSC_BUFFER_SIZE - nframes;
  memmove(osc_buffer, osc_buffer + nframes, move_size * sizeof(float));
  memcpy(osc_buffer + move_size, out, nframes * sizeof(float));

  return 0;
}

void drawpixel(SDL_Surface* surface, int x, int y, Uint32 color) {
  
  if (x < 0 || x >= surface->w || y < 0 || y >= surface->h) {
    return;
  }

  Uint32* pixels = (Uint32*)surface->pixels;
  pixels[y * surface->w + x] = color;
}

void fillrect(SDL_Surface* surface, int x, int y, int w, int h, Uint32 color) {
  for (int dx=x; dx<x+w; dx++) {
    for (int dy=y; dy<y+h; dy++) {
      drawpixel(surface, dx, dy, color);
    }
  }
}

void audioinit(void) {

  // setup jack
  const char* client_name = "jack_dev_test";
  jack_status_t status;

  client = jack_client_open(client_name, JackNullOption, &status);
  if (client == NULL) {
    fprintf(stderr, "jack_client_open failed\n");
    exit(1);
  }

  input_port = jack_port_register(client, "input", JACK_DEFAULT_AUDIO_TYPE, 
  				  JackPortIsInput, 0);
  output_port = jack_port_register(client, "output", JACK_DEFAULT_AUDIO_TYPE, 
				   JackPortIsOutput, 0);

  jack_set_process_callback(client, process, 0);

  if (jack_activate(client)) {
    fprintf(stderr, "cannot activate jack client\n");
    exit(1);
  }

  printf("jack client activated\n");
  
  const char **ports = jack_get_ports(client, NULL, NULL, 
  				      JackPortIsPhysical | JackPortIsOutput);
  if (ports == NULL) {
    fprintf(stderr, "no physical capture ports available\n");
    exit(1);
  }
  if (jack_connect(client, ports[0], jack_port_name(input_port))) {
    fprintf(stderr, "cannot connect to system audio input");
    exit(1);
  }
  free(ports);

  ports = jack_get_ports(client, NULL, NULL, 
			 JackPortIsPhysical | JackPortIsInput);
  if (ports == NULL) {
    fprintf(stderr, "no physical output ports available\n");
    exit(1);
  }
  if (jack_connect(client, jack_port_name(output_port), ports[0])) {
    fprintf(stderr, "cannot connect to system audio output 0");
    exit(1);
  }
  if (jack_connect(client, jack_port_name(output_port), ports[1])) {
    fprintf(stderr, "cannot connect to system audio output 1");
    exit(1);
  }
  free(ports);
}

void font_blit_string(SDL_Surface* font, SDL_Surface* screen, const char* string, int x, int y) {

  char c;
  int i = 0;

  while (1) {
    
    c = string[i];
    if (c == '\0') break;

    int fontindex = c - ' ';

    // get font source rect
    SDL_Rect srect = { w: font_char_width, h: font_char_height, 
		       x: fontindex * 16, 0};
    SDL_Rect drect = { w: font_char_width, h: font_char_height,
		       x: i * font_char_width + x,
		       y: y};
    SDL_BlitSurface(font, &srect, screen, &drect);
    i++;
  }
}

int main(int argc, char* argv[]) {

  // initialize sdl 1.2 for graphics and input
  if (SDL_Init(SDL_INIT_VIDEO) < 0) {
    printf("SDL could not initialize\n");
    return 1;
  }

  SDL_Surface* screen = SDL_SetVideoMode(screen_w, screen_h, 32, SDL_SWSURFACE);
  if (screen == NULL) {
    printf("window could not be created\n");
    return  1;
  }

  SDL_FillRect(screen, &screen->clip_rect, SDL_MapRGB(screen->format, 0xFF, 0x00, 0x00));

  SDL_Surface* temp = IMG_Load("new_font_2026_16x16.png");
  if (temp == NULL) {
    printf("unable to load image\n");
    return 1;
  }
  SDL_Surface* font = SDL_DisplayFormatAlpha(temp);
  SDL_FreeSurface(temp);
  if (font == NULL) {
    printf("failed to optimise font image\n");
    return 1;
  }  

  Uint32 osc_color = SDL_MapRGB(screen->format, 0, 255, 0);
  Uint32 focus_color = SDL_MapRGB(screen->format, 0, 0, 255);
  Uint32 title_color = SDL_MapRGB(screen->format, 100, 100, 100);

  // start jack client
  audioinit();

  // limit frame rate
  Uint32 start_tick;
  int frame_ticks;

  while (!quit) {

    start_tick = SDL_GetTicks();

    SDL_Event event;
    while (SDL_PollEvent(&event)) {

      if (event.type == SDL_QUIT) {
	quit = 1;
      }
      if (event.type == SDL_KEYDOWN) {
	if (event.key.keysym.sym == SDLK_ESCAPE) {
	  quit = 1;
	} else if (event.key.keysym.sym == SDLK_RIGHT) {
	  if (focusfield == testtone_on_field) {
	    testtone_on = !testtone_on;
	  } else if (focusfield == testtone_amp_field) {
	    testtone_amp += 0.1;
	    if (testtone_amp > 1.0) testtone_amp = 1.0;
	  } else if (focusfield == compress_makeup_field) {
	    makeup += 0.1;
	    if (makeup > 2.0) makeup = 2.0;
	  } else if (focusfield == compress_threshold_field) {
	    threshold += 0.1;
	    if (threshold > 1.0) threshold = 1.0;
	  } else if (focusfield == compress_ratio_field) {
	    ratio += 0.1;
	    if (ratio > 8.0) ratio = 8.0;
	  } else if (focusfield == compress_attack_field) {
	    attack += 0.01;
	    if (attack > 0.1) attack = 0.1;
	  } else if (focusfield == compress_release_field) {
	    release += 0.1;
	    if (release > 1.0) release = 1.0;
	  }
	  update = 1;
	} else if (event.key.keysym.sym == SDLK_LEFT) {
	  if (focusfield == testtone_on_field) {
	    testtone_on = !testtone_on;
	  } else if (focusfield == testtone_amp_field) {
	    testtone_amp -= 0.1;
	    if (testtone_amp < 0.0) testtone_amp = 0.0;
	  } else if (focusfield == compress_makeup_field) {
	    makeup -= 0.1;
	    if (makeup < 0.0) makeup = 0.0;
	  } else if (focusfield == compress_threshold_field) {
	    threshold -= 0.1;
	    if (threshold < 0.1) threshold = 0.1;
	  } else if (focusfield == compress_ratio_field) {
	    ratio -= 0.1;
	    if (ratio < 0.1) ratio = 0.1;
	  } else if (focusfield == compress_attack_field) {
	    attack -= 0.01;
	    if (attack < 0.01) attack = 0.01;
	  } else if (focusfield == compress_release_field) {
	    release -= 0.1;
	    if (release < 0.1) release = 0.1;
	  }
	  update = 1;
	} else if (event.key.keysym.sym == SDLK_DOWN) {
	  focusfield++;
	  if (focusfield >= field_count) focusfield = field_count - 1;
	  update = 1;
	} else if (event.key.keysym.sym == SDLK_UP) {
	  focusfield--;
	  if (focusfield < testtone_on_field) focusfield = testtone_on_field;
	  update = 1;
	}
      }
    }    

    // draw here
    if (update) {

      SDL_FillRect(screen, &screen->clip_rect, SDL_MapRGB(screen->format, 0x00, 0x00, 0x00));

      // draw oscilloscope
      int osc_trigger_i = 0;
      for (int i=1; i < OSC_BUFFER_SIZE; i++) {
	// find index of zero crossing
	if (osc_buffer[i-1] < 0 && osc_buffer[i] >= 0) {
	  osc_trigger_i = i;
	  break;
	}
      }
      if (SDL_MUSTLOCK(screen)) SDL_LockSurface(screen);
      for (int x=0; x<screen->w; x++) {
	int buf_pos = osc_trigger_i + x;
	if (buf_pos >= OSC_BUFFER_SIZE) break;

	// map float to screen height
	float val = osc_buffer[buf_pos];
	int y = (screen->h / 2) + (int)(val * screen->h / 2.1);
	drawpixel(screen, x, y, osc_color);
      }
      if (SDL_MUSTLOCK(screen)) SDL_UnlockSurface(screen);

      int line = 0;

      //      font_blit_string(font, screen,  "atracker", 16, 16);
      //      line++;

      const int slen = 255;
      char s[slen];
      int field_x = 16;
      int field_y = 16 + line * 16;
      int font_h = 16;
      int font_w = 16;

      char* label = "test tone";
      fillrect(screen, field_x, field_y, font_w * strlen(label), font_h, title_color);
      font_blit_string(font, screen, label, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      snprintf(s, slen, "tone: %s", testtone_on ? "on" : "off");
      if (focusfield == testtone_on_field) {
	fillrect(screen, field_x, field_y, font_w * strlen(s), font_h, focus_color);
      }
      font_blit_string(font, screen, s, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      snprintf(s, 255, "amp: %f", testtone_amp);
      if (focusfield == testtone_amp_field) {
	fillrect(screen, field_x, field_y, font_w * strlen(s), font_h, focus_color);
      }
      font_blit_string(font, screen, s, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      label = "compressor";
      fillrect(screen, field_x, field_y, font_w * strlen(label), font_h, title_color);
      font_blit_string(font, screen, label, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      snprintf(s, 255, "threshold: %f", threshold);
      if (focusfield == compress_threshold_field) {
	fillrect(screen, field_x, field_y, font_w * strlen(s), font_h, focus_color);
      }
      font_blit_string(font, screen, s, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      snprintf(s, 255, "makeup: %f", makeup);
      if (focusfield == compress_makeup_field) {
	fillrect(screen, field_x, field_y, font_w * strlen(s), font_h, focus_color);
      }
      font_blit_string(font, screen, s, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      snprintf(s, 255, "ratio: %f", ratio);
      if (focusfield == compress_ratio_field) {
	fillrect(screen, field_x, field_y, font_w * strlen(s), font_h, focus_color);
      }
      font_blit_string(font, screen, s, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      snprintf(s, 255, "attack: %f", attack);
      if (focusfield == compress_attack_field) {
	fillrect(screen, field_x, field_y, font_w * strlen(s), font_h, focus_color);
      }
      font_blit_string(font, screen, s, field_x, field_y);
      line++;

      field_y = 16 + line * 16;
      snprintf(s, 255, "release: %f", release);
      if (focusfield == compress_release_field) {
	fillrect(screen, field_x, field_y, font_w * strlen(s), font_h, focus_color);
      }
      font_blit_string(font, screen, s, field_x, field_y);
      line++;
      
      SDL_Flip(screen);
      //      SDL_Delay(16); // ~ 60fps
      //      update = 0;

      frame_ticks = SDL_GetTicks() - start_tick;
      if (frame_ticks < TICKS_PER_FRAME) {
	SDL_Delay(TICKS_PER_FRAME - frame_ticks);
      }
    }
  }

  jack_client_close(client);

  SDL_FreeSurface(font);
  SDL_Quit();
  
  return 0;
}
