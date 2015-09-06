#include <pebble.h>
#define INACTIVE_MESSAGE_SHOW_TIME 3000
#define SHAKE_INTERVAL 15000
#define WAIT_AFTER_SHAKE 2500

#define BACKGROUND GColorVividCerulean
#define BACKGROUND_ALERT GColorRed

static Window* window;
static TextLayer* text_layer;
static TextLayer* shakes_layer[3];
static TextLayer* inactive_message;

static int sensitivity;

static int shakes[3];
static char buf[3][10];

static bool count_shakes;

static AppTimer* timer_noshake;

static void number_shakes() {
  count_shakes = true;
}

static void hide_inactive_message(){
#ifdef PBL_PLATFORM_BASALT
	window_set_background_color(window, BACKGROUND);
#endif
	layer_set_hidden(text_layer_get_layer(inactive_message), true);
}

static void noshake() {
#ifdef PBL_PLATFORM_BASALT
	window_set_background_color(window, BACKGROUND_ALERT);
#endif
	layer_set_hidden(text_layer_get_layer(inactive_message), false);
	app_timer_register(INACTIVE_MESSAGE_SHOW_TIME, hide_inactive_message, NULL);
	vibes_double_pulse();
	timer_noshake = app_timer_register(SHAKE_INTERVAL, noshake, NULL);
	APP_LOG(APP_LOG_LEVEL_DEBUG, "*****No shake for 5 sec...");
}

// 0: nothing detected yet
// 1: "approach" detected
// 2: "impact" detected
// (when "retract" detected, will reset to 0)
//
// States are reset to 0 if nothing deetected in current batch
static int states[3] = {0, 0, 0};

static void detected(int i) {
	if(!count_shakes)
		return;

	APP_LOG(APP_LOG_LEVEL_DEBUG, "*** Detected event of type %d", i);

	count_shakes = false; // until timer rings
	shakes[i]++;
	app_timer_register(WAIT_AFTER_SHAKE, number_shakes, NULL);
	snprintf(buf[i], sizeof(buf[i]), "%d", shakes[i]);
	text_layer_set_text(shakes_layer[i], buf[i]);

	vibes_double_pulse();

	if(timer_noshake){
		app_timer_cancel(timer_noshake);
	}
	timer_noshake = app_timer_register(SHAKE_INTERVAL, noshake, NULL);

	//app_timer_reschedule(AppTimer * noshake, 10000);
	
	states[i] = 0; // reset
}

static void accel_data_handler(AccelData* data, uint32_t num_samples) {
	// Regular handshake: y ~= 1000; approach on X+ or Y; impact on Y
	// Fist bump: z ~= -1000; approach on X+; impact on X
	// High five: x ~= -900; approach on Z+; impact on Z
	
	// this is to easily detect if we changed any state in current run
	int ostates[3];
	for(int i=0; i<3; i++)
		ostates[i] = states[i];

	// calculate average of these
	int ax=0, ay=0, az=0;
	for(unsigned int i=0; i<num_samples; i++) {
		ax += data[i].x;
		ay += data[i].y;
		az += data[i].z;

		if(data[i].x > 0) {
			if(states[0] == 0)
				states[0] = 1; // approach found
			if(states[1] == 0)
				states[1] = 1;
		} else if(data[i].x < 0) {
			if(states[0] == 2)
				detected(0);
			if(states[1] == 2)
				detected(1);
		}
		if(data[i].z > 0) {
			if(states[2] == 0)
				states[2] = 1;
		} else if(data[i].z < 0) {
			if(states[2] == 2)
				detected(2);
		}
	}
	ax /= (int)num_samples;
	ay /= (int)num_samples;
	az /= (int)num_samples;
	APP_LOG(APP_LOG_LEVEL_DEBUG, "			 ax: %d, ay: %d, az: %d", ax, ay, az);

	// calculate *-axis derivatives
	int dx = abs(data[num_samples-1].x - data[0].x);
	int dy = abs(data[num_samples-1].y - data[0].y);
	int dz = abs(data[num_samples-1].z - data[0].z);

	APP_LOG(APP_LOG_LEVEL_DEBUG, "x: %d, y: %d, z: %d, dx: %d, dy: %d, dz: %d", data[0].x, data[0].y, data[0].z, dx, dy, dz);

	if(!count_shakes)
		return;

	if(dx > sensitivity && dx > dy && dx > dz) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Fist bump impact detected");
		if(states[1] == 1)
			states[1] = 2;
	}
	else if(dz > sensitivity && dz > dx && dz > dy) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "High five impact detected");
		if(states[2] == 1)
			states[2] = 2;
	}
	else if(dy > sensitivity && dy > dx && dy > dz) {
		APP_LOG(APP_LOG_LEVEL_DEBUG, "Regular handshake impact detected");
		if(states[0] == 1)
			states[0] = 2;
	}

	// if state not changed in this run, reset it to 0
	for(int i=0; i<3; i++)
		if(states[i] == ostates[i])
			states[i] = 0;
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {

	// Increase threshold
	static char buf[128];
	sensitivity += 25;
	snprintf(buf, sizeof(buf), "Threshold: %d", sensitivity);
	text_layer_set_text(text_layer, buf);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
	// Resume shock display
	static char buf[128];
	sensitivity -= 25;
	snprintf(buf, sizeof(buf), "Threshold: %d", sensitivity);
	text_layer_set_text(text_layer, buf);
}
static void click_config_provider(void *context) {
	window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
	window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void window_load(Window *window) {
	Layer *window_layer = window_get_root_layer(window);
	GRect bounds = layer_get_bounds(window_layer);

#ifdef PBL_PLATFORM_BASALT
	window_set_background_color(window, BACKGROUND);

	StatusBarLayer *sb = status_bar_layer_create();
	status_bar_layer_set_colors(sb, GColorClear, GColorWhite);
	layer_add_child(window_layer, status_bar_layer_get_layer(sb));

#define TOP_SHIFT STATUS_BAR_LAYER_HEIGHT
#else
#define TOP_SHIFT 0
#endif

	TextLayer *title = text_layer_create((GRect) { .origin = { 0, TOP_SHIFT}, .size = { bounds.size.w, 60 } });
#ifdef PBL_PLATFORM_BASALT
	text_layer_set_background_color(title, GColorClear);
	text_layer_set_text_color(title, GColorWhite);
#endif
	text_layer_set_text(title, "GREET");
	text_layer_set_font(title, fonts_get_system_font(FONT_KEY_BITHAM_30_BLACK));
	layer_add_child(window_layer, text_layer_get_layer(title));

	char* titles[] = {
		"Shakes",
		"Fist Bumps",
		"High Fives",
	};
	for(int i=0; i<3; i++) {
		TextLayer *tl = text_layer_create((GRect) { .origin = { 0, TOP_SHIFT + 35 +  30*i}, .size = { bounds.size.w/3*2, 30 } });
#ifdef PBL_PLATFORM_BASALT
		text_layer_set_background_color(tl, GColorClear);
		text_layer_set_text_color(tl, GColorWhite);
#endif
		text_layer_set_text(tl, titles[i]);
		text_layer_set_font(tl, fonts_get_system_font(FONT_KEY_GOTHIC_24));
		layer_add_child(window_layer, text_layer_get_layer(tl));

		shakes_layer[i] = text_layer_create((GRect) { .origin = { bounds.size.w/3*2, TOP_SHIFT + 35 +  30*i}, .size = { bounds.size.w/3, 30 } });
#ifdef PBL_PLATFORM_BASALT
		text_layer_set_background_color(shakes_layer[i], GColorClear);
		text_layer_set_text_color(shakes_layer[i], GColorWhite);
#endif
		text_layer_set_text(shakes_layer[i], "0");
		text_layer_set_font(shakes_layer[i], fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
		layer_add_child(window_layer, text_layer_get_layer(shakes_layer[i]));
	}

	inactive_message = text_layer_create((GRect) { .origin = { 0, TOP_SHIFT + 35}, .size = { bounds.size.w, 30 } });
#ifdef PBL_PLATFORM_BASALT
	text_layer_set_background_color(inactive_message , BACKGROUND_ALERT);
	text_layer_set_text_color(inactive_message , GColorWhite);
#endif
	text_layer_set_text(inactive_message, "You're too shy.");
	text_layer_set_font(inactive_message, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
	layer_set_hidden(text_layer_get_layer(inactive_message), true);
	layer_add_child(window_layer, text_layer_get_layer(inactive_message));

	// underline title
	TextLayer *line = text_layer_create((GRect) { .origin = { 0, TOP_SHIFT+33}, .size = { bounds.size.w, 3 } });
	text_layer_set_background_color(line, COLOR_FALLBACK(GColorWhite, GColorBlack));
	layer_add_child(window_layer, text_layer_get_layer(line));

	// this is for debugging...
	text_layer = text_layer_create((GRect) { .origin = { 0, bounds.size.h-20}, .size = { bounds.size.w, 20 } });
	text_layer_set_text(text_layer, "");
#ifdef PBL_PLATFORM_BASALT
	text_layer_set_background_color(text_layer , GColorClear);
	text_layer_set_text_color(text_layer, GColorWhite);
#endif
	layer_add_child(window_layer, text_layer_get_layer(text_layer));
}


static void window_unload(Window *window) {
	text_layer_destroy(text_layer);
}

static void init(void) {
	window = window_create();
	window_set_click_config_provider(window, click_config_provider);
	window_set_window_handlers(window, (WindowHandlers) {
			.load = window_load,
			.unload = window_unload,
			});
	sensitivity = 980;
	shakes[0] = 0;
	shakes[1] = 0;
	shakes[2] = 0;
	count_shakes = true;
	timer_noshake = app_timer_register(SHAKE_INTERVAL, noshake, NULL);
  

  APP_LOG(APP_LOG_LEVEL_DEBUG, "Timer Registered...");
  
	vibes_double_pulse();
	accel_data_service_subscribe(5, accel_data_handler);
	const bool animated = true;
	window_stack_push(window, animated);
}

static void deinit(void) {
	window_destroy(window);
}

int main(void) {
	init();

	APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing, pushed window: %p", window);

	app_event_loop();
	deinit();
}
