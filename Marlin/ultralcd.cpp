#include "temperature.h"
#include "ultralcd.h"
#ifdef ULTRA_LCD
#include "Marlin.h"
#include "language.h"
#include "cardreader.h"
#include "temperature.h"
#include "stepper.h"
#include "ConfigurationStore.h"

int8_t encoderDiff; /* encoderDiff is updated from interrupt context and added to encoderPosition every LCD update */

/* Configuration settings */
int plaPreheatHotendTemp;
int plaPreheatHPBTemp;
int plaPreheatFanSpeed;


int absPreheatHotendTemp;
int absPreheatHPBTemp;
int absPreheatFanSpeed;

int filament_seq = 0;
int pausa_display = 0;
int st_message = 0;
int encoder_zero = 0;
int fil_temp = 0;

static uint8_t tmp_extruder;

#ifdef FILAMENT_LCD_DISPLAY
  unsigned long message_millis = 0;
#endif

#ifdef ULTIPANEL
  static float manual_feedrate[] = MANUAL_FEEDRATE;
#endif // ULTIPANEL

/* !Configuration settings */

//Function pointer to menu functions.
typedef void (*menuFunc_t)();

uint8_t lcd_status_message_level;
char lcd_status_message[LCD_WIDTH+1] = WELCOME_MSG;

#ifdef DOGLCD
#include "dogm_lcd_implementation.h"
#else
#include "ultralcd_implementation_hitachi_HD44780.h"
#endif

/** forward declarations **/

void copy_and_scalePID_i();
void copy_and_scalePID_d();

/* Different menus */
static void lcd_status_screen();
#ifdef ULTIPANEL
extern bool powersupply;
static void lcd_main_menu();
static void lcd_tune_menu();
static void lcd_filamento_menu();
static void lcd_precalentar_menu();
static void lcd_herramientas_menu();
static void lcd_calibrate_bed_menu();
static void lcd_cargar_filamento_menu();
static void lcd_descargar_filamento_menu();
static void lcd_move_menu();
static void lcd_temp_config_menu();
static void lcd_control_temperature_preheat_pla_settings_menu();
static void lcd_control_temperature_preheat_abs_settings_menu();
static void lcd_control_temperature_preheat_flex_settings_menu();
static void lcd_control_motion_menu();
static void lcd_control_volumetric_menu();
#ifdef DOGLCD
static void lcd_set_contrast();
#endif
static void lcd_control_retract_menu();
static void lcd_sdcard_menu();


static void lcd_quick_feedback();//Cause an LCD refresh, and give the user visual or audible feedback that something has happened

/* Different types of actions that can be used in menu items. */
static void menu_action_back(menuFunc_t data);
static void menu_action_submenu(menuFunc_t data);
static void menu_action_gcode(const char* pgcode);
static void menu_action_function(menuFunc_t data);
static void menu_action_sdfile(const char* filename, char* longFilename);
static void menu_action_sddirectory(const char* filename, char* longFilename);
static void menu_action_setting_edit_bool(const char* pstr, bool* ptr);
static void menu_action_setting_edit_int3(const char* pstr, int* ptr, int minValue, int maxValue);
static void menu_action_setting_edit_float3(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float32(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float43(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float5(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float51(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_float52(const char* pstr, float* ptr, float minValue, float maxValue);
static void menu_action_setting_edit_long5(const char* pstr, unsigned long* ptr, unsigned long minValue, unsigned long maxValue);
static void menu_action_setting_edit_callback_bool(const char* pstr, bool* ptr, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_int3(const char* pstr, int* ptr, int minValue, int maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float3(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float32(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float43(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float5(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float51(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_float52(const char* pstr, float* ptr, float minValue, float maxValue, menuFunc_t callbackFunc);
static void menu_action_setting_edit_callback_long5(const char* pstr, unsigned long* ptr, unsigned long minValue, unsigned long maxValue, menuFunc_t callbackFunc);

#define ENCODER_FEEDRATE_DEADZONE 10

#if !defined(LCD_I2C_VIKI)
  #ifndef ENCODER_STEPS_PER_MENU_ITEM
    #define ENCODER_STEPS_PER_MENU_ITEM 5
  #endif
  #ifndef ENCODER_PULSES_PER_STEP
    #define ENCODER_PULSES_PER_STEP 1
  #endif
#else
  #ifndef ENCODER_STEPS_PER_MENU_ITEM
    #define ENCODER_STEPS_PER_MENU_ITEM 2 // VIKI LCD rotary encoder uses a different number of steps per rotation
  #endif
  #ifndef ENCODER_PULSES_PER_STEP
    #define ENCODER_PULSES_PER_STEP 1
  #endif
#endif


/* Helper macros for menus */
#define START_MENU() do { \
    if (encoderPosition > 0x8000) encoderPosition = 0; \
    if (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM < currentMenuViewOffset) currentMenuViewOffset = encoderPosition / ENCODER_STEPS_PER_MENU_ITEM;\
    uint8_t _lineNr = currentMenuViewOffset, _menuItemNr; \
    bool wasClicked = LCD_CLICKED;\
    for(uint8_t _drawLineNr = 0; _drawLineNr < LCD_HEIGHT; _drawLineNr++, _lineNr++) { \
        _menuItemNr = 0;
#define MENU_ITEM(type, label, args...) do { \
    if (_menuItemNr == _lineNr) { \
        if (lcdDrawUpdate) { \
            const char* _label_pstr = PSTR(label); \
            if ((encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) == _menuItemNr) { \
                lcd_implementation_drawmenu_ ## type ## _selected (_drawLineNr, _label_pstr , ## args ); \
            }else{\
                lcd_implementation_drawmenu_ ## type (_drawLineNr, _label_pstr , ## args ); \
            }\
        }\
        if (wasClicked && (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) == _menuItemNr) {\
            lcd_quick_feedback(); \
            menu_action_ ## type ( args ); \
            return;\
        }\
    }\
    _menuItemNr++;\
} while(0)
#define MENU_ITEM_DUMMY() do { _menuItemNr++; } while(0)
#define MENU_ITEM_EDIT(type, label, args...) MENU_ITEM(setting_edit_ ## type, label, PSTR(label) , ## args )
#define MENU_ITEM_EDIT_CALLBACK(type, label, args...) MENU_ITEM(setting_edit_callback_ ## type, label, PSTR(label) , ## args )
#define END_MENU() \
    if (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM >= _menuItemNr) encoderPosition = _menuItemNr * ENCODER_STEPS_PER_MENU_ITEM - 1; \
    if ((uint8_t)(encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) >= currentMenuViewOffset + LCD_HEIGHT) { currentMenuViewOffset = (encoderPosition / ENCODER_STEPS_PER_MENU_ITEM) - LCD_HEIGHT + 1; lcdDrawUpdate = 1; _lineNr = currentMenuViewOffset - 1; _drawLineNr = -1; } \
    } } while(0)

/** Used variables to keep track of the menu */
#ifndef REPRAPWORLD_KEYPAD
volatile uint8_t buttons;//Contains the bits of the currently pressed buttons.
#else
volatile uint8_t buttons_reprapworld_keypad; // to store the reprapworld_keypad shift register values
#endif
#ifdef LCD_HAS_SLOW_BUTTONS
volatile uint8_t slow_buttons;//Contains the bits of the currently pressed buttons.
#endif
uint8_t currentMenuViewOffset;              /* scroll offset in the current menu */
uint32_t blocking_enc;
uint8_t lastEncoderBits;
uint32_t encoderPosition;
#if (SDCARDDETECT > 0)
bool lcd_oldcardstatus;
#endif
#endif //ULTIPANEL

menuFunc_t currentMenu = lcd_status_screen; /* function pointer to the currently active menu */
uint32_t lcd_next_update_millis;
uint8_t lcd_status_update_delay;
bool ignore_click = false;
bool wait_for_unclick;
uint8_t lcdDrawUpdate = 2;                  /* Set to none-zero when the LCD needs to draw, decreased after every draw. Set to 2 in LCD routines so the LCD gets at least 1 full redraw (first redraw is partial) */

//prevMenu and prevEncoderPosition are used to store the previous menu location when editing settings.
menuFunc_t prevMenu = NULL;
uint16_t prevEncoderPosition;
//Variables used when editing values.
const char* editLabel;
void* editValue;
int32_t minEditValue, maxEditValue;
menuFunc_t callbackFunc;

// place-holders for Ki and Kd edits, and the extruder # being edited
float raw_Ki, raw_Kd;
int pid_current_extruder;

static void lcd_goto_menu(menuFunc_t menu, const uint32_t encoder=0, const bool feedback=true) {
  if (currentMenu != menu) {
    currentMenu = menu;
    encoderPosition = encoder;
    if (feedback) lcd_quick_feedback();

    // For LCD_PROGRESS_BAR re-initialize the custom characters
    #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT) && !defined(DOGLCD)
      lcd_set_custom_characters(menu == lcd_status_screen);
    #endif
  }
}

/* Main status screen. It's up to the implementation specific part to show what is needed. As this is very display dependent */
static void lcd_status_screen()
{
  #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT) && !defined(DOGLCD)
    uint16_t mil = millis();
    #ifndef PROGRESS_MSG_ONCE
      if (mil > progressBarTick + PROGRESS_BAR_MSG_TIME + PROGRESS_BAR_BAR_TIME) {
        progressBarTick = mil;
      }
    #endif
    #if PROGRESS_MSG_EXPIRE > 0
      // keep the message alive if paused, count down otherwise
      if (messageTick > 0) {
        if (card.isFileOpen()) {
          if (IS_SD_PRINTING) {
            if ((mil-messageTick) >= PROGRESS_MSG_EXPIRE) {
              lcd_status_message[0] = '\0';
              messageTick = 0;
            }
          }
          else {
            messageTick += LCD_UPDATE_INTERVAL;
          }
        }
        else {
          messageTick = 0;
        }
      }
    #endif
  #endif //LCD_PROGRESS_BAR

    if (lcd_status_update_delay)
        lcd_status_update_delay--;
    else
        lcdDrawUpdate = 1;

    if (lcdDrawUpdate) {
        lcd_implementation_status_screen();
        lcd_status_update_delay = 10;   /* redraw the main screen every second. This is easier then trying keep track of all things that change on the screen */
    }

#ifdef ULTIPANEL

    bool current_click = LCD_CLICKED;

    if (ignore_click) {
        if (wait_for_unclick) {
          if (!current_click) {
              ignore_click = wait_for_unclick = false;
          }
          else {
              current_click = false;
          }
        }
        else if (current_click) {
            lcd_quick_feedback();
            wait_for_unclick = true;
            current_click = false;
        }
    }

    if (current_click)
    {
        lcd_goto_menu(lcd_main_menu);
        lcd_implementation_init( // to maybe revive the LCD if static electricity killed it.
          #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT) && !defined(DOGLCD)
            currentMenu == lcd_status_screen
          #endif
        );
        #ifdef FILAMENT_LCD_DISPLAY
          message_millis = millis();  // get status message to show up for a while
        #endif
    }

#ifdef ULTIPANEL_FEEDMULTIPLY
    // Dead zone at 100% feedrate
    if ((feedmultiply < 100 && (feedmultiply + int(encoderPosition)) > 100) ||
            (feedmultiply > 100 && (feedmultiply + int(encoderPosition)) < 100))
    {
        encoderPosition = 0;
        feedmultiply = 100;
    }

    if (feedmultiply == 100 && int(encoderPosition) > ENCODER_FEEDRATE_DEADZONE)
    {
        feedmultiply += int(encoderPosition) - ENCODER_FEEDRATE_DEADZONE;
        encoderPosition = 0;
    }
    else if (feedmultiply == 100 && int(encoderPosition) < -ENCODER_FEEDRATE_DEADZONE)
    {
        feedmultiply += int(encoderPosition) + ENCODER_FEEDRATE_DEADZONE;
        encoderPosition = 0;
    }
    else if (feedmultiply != 100)
    {
        feedmultiply += int(encoderPosition);
        encoderPosition = 0;
    }
#endif //ULTIPANEL_FEEDMULTIPLY

    if (feedmultiply < 10)
        feedmultiply = 10;
    else if (feedmultiply > 999)
        feedmultiply = 999;
#endif //ULTIPANEL
}

#ifdef ULTIPANEL

static void lcd_return_to_status() { lcd_goto_menu(lcd_status_screen, 0, false); }

static void lcd_sdcard_pause() { card.pauseSDPrint(); }

static void lcd_sdcard_resume() { card.startFileprint(); }

static void lcd_sdcard_stop()
{
    card.sdprinting = false;
    card.closefile();
    quickStop();
    if(SD_FINISHED_STEPPERRELEASE)
    {
        enquecommand_P(PSTR(SD_FINISHED_RELEASECOMMAND));
    }
    autotempShutdown();

	cancel_heatup = true;

	lcd_setstatus(MSG_PRINT_ABORTED);
}

 /* Menu implementation */
 
 
////iSM
static void lcd_main_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_WATCH, lcd_status_screen);
    if (movesplanned() || IS_SD_PRINTING)
    {
	   MENU_ITEM(submenu, MSG_TUNE, lcd_tune_menu);//1     
    }
#ifdef SDSUPPORT
    if (card.cardOK)
    {
        if (card.isFileOpen())
        {
            if (card.sdprinting)
                //MENU_ITEM(function, MSG_PAUSE_PRINT, lcd_sdcard_pause);
				MENU_ITEM(gcode, MSG_FILAMENT_CH, PSTR("M600"));
            else
                MENU_ITEM(function, MSG_RESUME_PRINT, lcd_sdcard_resume);
            MENU_ITEM(function, MSG_STOP_PRINT, lcd_sdcard_stop);
			
        }else{
            MENU_ITEM(submenu, MSG_CARD_MENU, lcd_sdcard_menu);
#if SDCARDDETECT < 1
            MENU_ITEM(gcode, MSG_CNG_SDCARD, PSTR("M21"));  // SD-card changed by user
#endif
        }
    }else{
        MENU_ITEM(submenu, MSG_NO_CARD, lcd_sdcard_menu);
#if SDCARDDETECT < 1
        MENU_ITEM(gcode, MSG_INIT_SDCARD, PSTR("M21")); // Manually initialize the SD-card via user interface
#endif
    }
#endif
   if (!movesplanned() && !IS_SD_PRINTING)
	{
	MENU_ITEM(submenu, MSG_FILAMENTO, lcd_filamento_menu);//2
	MENU_ITEM(submenu, MSG_PRECALENTAR, lcd_precalentar_menu);//3
	MENU_ITEM(submenu, MSG_HERRAMIENTAS, lcd_herramientas_menu);//4
	}
 END_MENU();
}

#ifdef SDSUPPORT
static void lcd_autostart_sd()
{
    card.lastnr=0;
    card.setroot();
    card.checkautostart(true);
}
#endif
////i1
static void lcd_tune_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    MENU_ITEM_EDIT(int3, MSG_SPEED, &feedmultiply, 10, 999);
    MENU_ITEM_EDIT(int3, MSG_NOZZLE, &target_temperature[0], 0, HEATER_0_MAXTEMP - 5);
    MENU_ITEM_EDIT(int3, MSG_BED, &target_temperature_bed, 0, BED_MAXTEMP - 15);
    MENU_ITEM_EDIT(int3, MSG_FAN_SPEED, &fanSpeed, 0, 255);
	MENU_ITEM_EDIT(int3, MSG_FLOW, &extrudemultiply, 10, 999);
    END_MENU();
}
////f1

///i2F
static void lcd_filamento_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    MENU_ITEM(submenu, MSG_CARGAR_FILAMENTO, lcd_cargar_filamento_menu);
    MENU_ITEM(submenu, MSG_DESCARGAR_FILAMENTO, lcd_descargar_filamento_menu);
	END_MENU(); 
}

float move_menu_scale;
static void lcd_status_screen();
static void lcd_move_menu_axis();


static void lcd_fc()
{
if (fil_temp == 200)
{
setTargetHotend0(plaPreheatHotendTemp);
}
if (fil_temp == 230)
{
setTargetHotend0(absPreheatHotendTemp);
}
setWatch(); // heater sanity check timer
if (!isHeatingHotend(0)&&(filament_seq == 0))
{
filament_seq = 1;
}
if ((filament_seq == 0)&&(pausa_display == 0))
{
pausa_display = 1;
}
if (filament_seq == 1)
{   
if (st_message == 0)  
    {
	lcdDrawUpdate = 2;  
	lcd_implementation_message0(PSTR("Introducir el nuevo"));
    lcd_implementation_message1(PSTR("filamento y girar el  "));
	lcd_implementation_message2(PSTR("boton hasta que"));
	lcd_implementation_message3(PSTR("salga el filamento"));
	if (encoder_zero == 0)
	{
	encoderPosition = 0;
	encoder_zero = 1;
	}
	}
    if (encoderPosition != 0)
    {
	    move_menu_scale = 1.0;
        current_position[E_AXIS] += float((int)encoderPosition) * move_menu_scale;
        encoderPosition = 0;
		 #ifdef DELTA
        calculate_delta(current_position);
        plan_buffer_line(delta[X_AXIS], delta[Y_AXIS], delta[Z_AXIS], current_position[E_AXIS], manual_feedrate[E_AXIS]/60, active_extruder);
        #else
        plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], manual_feedrate[E_AXIS]/60, active_extruder);
        #endif
        lcdDrawUpdate = 2;
		st_message = 1;
    }
	
    if ((lcdDrawUpdate) && (st_message == 1))
    {
	lcd_implementation_drawedit(PSTR("Extruder"), ftostr31(current_position[E_AXIS]));
    }

}
else
    {
    lcd_implementation_message4(PSTR(" Calentando..."));
    _draw_heater_status(6, 0);
	lcdDrawUpdate = 1;
	u8g.drawBitmapP(9,1,STATUS_SCREENBYTEWIDTH,STATUS_SCREENHEIGHT, (blink % 2) && fanSpeed ? status_screen0_bmp : status_screen1_bmp);
	
	}
	
if ((LCD_CLICKED) || (degTargetHotend(0) == 0)  || (currentMenu == lcd_status_screen))
	{
 	filament_seq = 0;
	st_message = 0;
	pausa_display = 0;
	encoder_zero = 0;
	setTargetHotend0(0);
	lcd_return_to_status();
    }
}
static void lcd_fc_pla()
{
fil_temp = 200;
lcd_fc();
}
static void lcd_fc_abs()
{
fil_temp = 230;
lcd_fc();
}

static void lcd_fd()
{
if (fil_temp == 200)
{
setTargetHotend0(plaPreheatHotendTemp);
}
if (fil_temp == 230)
{
setTargetHotend0(absPreheatHotendTemp);
}
setWatch(); // heater sanity check timer
if (!isHeatingHotend(0)&&(filament_seq == 0))
{
filament_seq = 1;
}
if ((filament_seq == 0)&&(pausa_display == 0))
{
pausa_display = 1;
}
if (filament_seq == 1)
{   
if (st_message == 0)  
    {
	lcdDrawUpdate = 2;  
	lcd_implementation_message0(PSTR(""));
    lcd_implementation_message1(PSTR("Se puede sacar"));
	lcd_implementation_message2(PSTR("el filamento.."));
	lcd_implementation_message3(PSTR(""));
	}
}
else
    {
    lcd_implementation_message4(PSTR(" Calentando..."));
    _draw_heater_status(6, 0);
	lcdDrawUpdate = 1;
	u8g.drawBitmapP(9,1,STATUS_SCREENBYTEWIDTH,STATUS_SCREENHEIGHT, (blink % 2) && fanSpeed ? status_screen0_bmp : status_screen1_bmp);
	
	}
	
if ((LCD_CLICKED) || (degTargetHotend(0) == 0)  || (currentMenu == lcd_status_screen))
	{
 	filament_seq = 0;
	st_message = 0;
	pausa_display = 0;
	encoder_zero = 0;
	setTargetHotend0(0);
	lcd_return_to_status();
    }
}

static void lcd_fd_pla()
{
fil_temp = 200;
lcd_fd();
}
static void lcd_fd_abs()
{
fil_temp = 230;
lcd_fd();
}

static void lcd_cargar_filamento_menu()
{
 START_MENU();
    MENU_ITEM(back, MSG_FILAMENTO, lcd_filamento_menu);
    MENU_ITEM(submenu, MSG_FC_PLA, lcd_fc_pla);
    MENU_ITEM(submenu, MSG_FC_ABS, lcd_fc_abs);
	END_MENU(); 
}

static void lcd_descargar_filamento_menu()
{
 START_MENU();
    MENU_ITEM(back, MSG_FILAMENTO, lcd_filamento_menu);
    MENU_ITEM(submenu, MSG_FC_PLA, lcd_fd_pla);
    MENU_ITEM(submenu, MSG_FC_ABS, lcd_fd_abs);
	END_MENU(); 
}
///f2F



///i3P
void lcd_preheat_pla0()
{
    setTargetHotend0(plaPreheatHotendTemp);
    setTargetBed(plaPreheatHPBTemp);
    fanSpeed = plaPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

void lcd_preheat_abs0()
{
    setTargetHotend0(absPreheatHotendTemp);
    setTargetBed(absPreheatHPBTemp);
    fanSpeed = absPreheatFanSpeed;
    lcd_return_to_status();
    setWatch(); // heater sanity check timer
}

// lcd_preheat_flex0()
//{
//    setTargetHotend0(flexPreheatHotendTemp);
//    setTargetBed(flexPreheatHPBTemp);
//    fanSpeed = flexPreheatFanSpeed;
//    lcd_return_to_status();
//    setWatch(); // heater sanity check timer
//}

static void lcd_precalentar_menu() 
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
#if TEMP_SENSOR_0 != 0
    MENU_ITEM(function, MSG_PREHEAT_PLA, lcd_preheat_pla0);
    MENU_ITEM(function, MSG_PREHEAT_ABS, lcd_preheat_abs0);
//	MENU_ITEM(function, MSG_PREHEAT_FLEX, lcd_preheat_flex0);
  #endif
#endif
END_MENU();
}
////f3P

////i4H

////i4
void lcd_cooldown()
{
    setTargetHotend0(0);
    setTargetBed(0);
    fanSpeed = 0;
    lcd_return_to_status();
}

////i5
void lcd_home_all()
{
enquecommand_P(PSTR("G28 X0 Y0"));
enquecommand_P(PSTR("G1 X20 Y20"));
enquecommand_P(PSTR("G28 Z0"));
enquecommand_P(PSTR("G1 Z5"));
}

////f4

static void lcd_herramientas_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
	MENU_ITEM(submenu, MSG_MOVE_AXIS, lcd_move_menu);//1
    MENU_ITEM(submenu, MSG_CALIBRATE_BED, lcd_calibrate_bed_menu);//2
	MENU_ITEM(submenu, MSG_TEMP_CONFIG, lcd_temp_config_menu);//3
	MENU_ITEM(function, MSG_COOLDOWN, lcd_cooldown);//4
	MENU_ITEM(function, MSG_AUTO_HOME, lcd_home_all);//5
	MENU_ITEM(gcode, MSG_DISABLE_STEPPERS, PSTR("M18"));//6
    END_MENU();
}


////i1
static void _lcd_move(const char *name, int axis, int min, int max) {
  if (encoderPosition != 0) {
    refresh_cmd_timeout();
    current_position[axis] += float((int)encoderPosition) * move_menu_scale;
    if (min_software_endstops && current_position[axis] < min) current_position[axis] = min;
    if (max_software_endstops && current_position[axis] > max) current_position[axis] = max;
    encoderPosition = 0;
    #ifdef DELTA
      calculate_delta(current_position);
      plan_buffer_line(delta[X_AXIS], delta[Y_AXIS], delta[Z_AXIS], current_position[E_AXIS], manual_feedrate[axis]/60, active_extruder);
    #else
      plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], manual_feedrate[axis]/60, active_extruder);
    #endif
    lcdDrawUpdate = 1;
  }
  if (lcdDrawUpdate) lcd_implementation_drawedit(name, ftostr31(current_position[axis]));
  if (LCD_CLICKED) lcd_goto_menu(lcd_move_menu_axis);
}
static void lcd_move_x() { _lcd_move(PSTR("X"), X_AXIS, X_MIN_POS, X_MAX_POS); }
static void lcd_move_y() { _lcd_move(PSTR("Y"), Y_AXIS, Y_MIN_POS, Y_MAX_POS); }
static void lcd_move_z() { _lcd_move(PSTR("Z"), Z_AXIS, Z_MIN_POS, Z_MAX_POS); }

static void lcd_move_e()
{
    if (encoderPosition != 0)
    {
        current_position[E_AXIS] += float((int)encoderPosition) * move_menu_scale;
        encoderPosition = 0;
        #ifdef DELTA
        calculate_delta(current_position);
        plan_buffer_line(delta[X_AXIS], delta[Y_AXIS], delta[Z_AXIS], current_position[E_AXIS], manual_feedrate[E_AXIS]/60, active_extruder);
        #else
        plan_buffer_line(current_position[X_AXIS], current_position[Y_AXIS], current_position[Z_AXIS], current_position[E_AXIS], manual_feedrate[E_AXIS]/60, active_extruder);
        #endif
        lcdDrawUpdate = 1;
    }
    if (lcdDrawUpdate)
    {
        lcd_implementation_drawedit(PSTR("Extruder"), ftostr31(current_position[E_AXIS]));
    }
    if (LCD_CLICKED) lcd_goto_menu(lcd_move_menu_axis);
}

static void lcd_move_menu_axis()
{
    START_MENU();
    MENU_ITEM(back, MSG_HERRAMIENTAS, lcd_herramientas_menu);
    MENU_ITEM(submenu, MSG_MOVE_X, lcd_move_x);
    MENU_ITEM(submenu, MSG_MOVE_Y, lcd_move_y);
    if (move_menu_scale < 10.0)
    {
        MENU_ITEM(submenu, MSG_MOVE_Z, lcd_move_z);
        MENU_ITEM(submenu, MSG_MOVE_E, lcd_move_e);
    }
    END_MENU();
}

static void lcd_move_menu_10mm()
{
    move_menu_scale = 10.0;
    lcd_move_menu_axis();
}
static void lcd_move_menu_1mm()
{
    move_menu_scale = 1.0;
    lcd_move_menu_axis();
}
static void lcd_move_menu_01mm()
{
    move_menu_scale = 0.1;
    lcd_move_menu_axis();
}

static void lcd_move_menu()
{
    START_MENU();
    MENU_ITEM(back, MSG_MOVE_AXIS, lcd_move_menu_axis);//error1
    MENU_ITEM(submenu, MSG_MOVE_01MM, lcd_move_menu_01mm);
    MENU_ITEM(submenu, MSG_MOVE_1MM, lcd_move_menu_1mm);
	MENU_ITEM(submenu, MSG_MOVE_10MM, lcd_move_menu_10mm);
    //TODO:X,Y,Z,E
    END_MENU();
}
////f1


////i2
static void lcd_calibrate_bed()
{
enquecommand_P(PSTR("G21"));//set units to millimeters
if (fil_temp == 200)
{
enquecommand_P(PSTR("M190 S60"));//wait for bed temperature to be reached
enquecommand_P(PSTR("M104 S200"));//set temperature
}
if (fil_temp == 230)	
{
enquecommand_P(PSTR("M190 S80"));//wait for bed temperature to be reached
enquecommand_P(PSTR("M104 S230"));//set temperature
}
enquecommand_P(PSTR("G28 X0 Y0"));//Home x and y axis
enquecommand_P(PSTR("G1 X20 Y20 F4000"));//Center extruder above bed
enquecommand_P(PSTR("G28 Z0"));
enquecommand_P(PSTR("G1 Z5 F5000"));//lift nozzle
if (fil_temp == 200)
{
enquecommand_P(PSTR("M109 S200"));//set temperature
}
if (fil_temp == 230)	
{
enquecommand_P(PSTR("M109 S230"));//set temperature
}
enquecommand_P(PSTR("G90"));//use absolute coordinates
enquecommand_P(PSTR("G92 E0"));
enquecommand_P(PSTR("M82"));//use absolute distances for extrusion
//if (fil_temp == 200)
//{
//enquecommand_P(PSTR("M106 S255"));
//}
enquecommand_P(PSTR("G1 E-1.00000 F1020.00000"));//
enquecommand_P(PSTR("G92 E0"));//
enquecommand_P(PSTR("G1 Z0.250 F6000.000"));//
enquecommand_P(PSTR("G1 X27.500 Y27.500 F6000.000"));//
enquecommand_P(PSTR("G1 E1.00000 F1020.00000"));//
enquecommand_P(PSTR("G1 X272.290 Y27.500 E10.32102 F1200.000"));//
enquecommand_P(PSTR("G1 X272.464 Y27.536 E10.32778"));//
enquecommand_P(PSTR("G1 X272.500 Y27.710 E10.33455"));//
enquecommand_P(PSTR("G1 X272.500 Y272.290 E19.64757"));//
enquecommand_P(PSTR("G1 X272.464 Y272.464 E19.65433"));//
enquecommand_P(PSTR("G1 X272.290 Y272.500 E19.66110"));//
enquecommand_P(PSTR("G1 X27.710 Y272.500 E28.97412"));//
enquecommand_P(PSTR("G1 X27.536 Y272.464 E28.98089"));//
enquecommand_P(PSTR("G1 X27.500 Y272.290 E28.98765"));//
enquecommand_P(PSTR("G1 X27.500 Y34.710 E38.03413"));//
enquecommand_P(PSTR("G1 X27.536 Y34.536 E38.04089"));//
enquecommand_P(PSTR("G1 X27.710 Y34.500 E38.04766"));//
enquecommand_P(PSTR("G1 X260.459 Y34.500 E46.91018"));//
enquecommand_P(PSTR("G1 E45.91018 F1020.00000"));//
enquecommand_P(PSTR("G92 E0"));//
enquecommand_P(PSTR("G1 Z3 F6000"));//
//if (fil_temp == 200)
//{
//enquecommand_P(PSTR("M107"));
//}
enquecommand_P(PSTR("G28 X0"));//turn off temperature
enquecommand_P(PSTR("M84"));
enquecommand_P(PSTR("G28 X0"));//home X axis
enquecommand_P(PSTR("M84"));//disable motors
enquecommand_P(PSTR("M140 S0"));//turn off heatbed
fil_temp = 0;
lcd_return_to_status();
}

static void lcd_cal_pla()
{
fil_temp = 200;
lcd_calibrate_bed();
}	
	
static void lcd_cal_abs()
{
fil_temp = 230;	
lcd_calibrate_bed();	
}	


static void lcd_calibrate_bed_menu()
{
 START_MENU();
    MENU_ITEM(back, MSG_HERRAMIENTAS, lcd_herramientas_menu);
    MENU_ITEM(submenu, MSG_CAL_PLA, lcd_cal_pla);
    MENU_ITEM(submenu, MSG_CAL_ABS, lcd_cal_abs);
    END_MENU(); 
}
////f2


////i3

static void lcd_temp_config_menu()
{
#ifdef PIDTEMP
    // set up temp variables - undo the default scaling
    raw_Ki = unscalePID_i(Ki);
    raw_Kd = unscalePID_d(Kd);
#endif

    START_MENU();
    MENU_ITEM(back, MSG_HERRAMIENTAS, lcd_herramientas_menu);
    MENU_ITEM_EDIT(int3, MSG_NOZZLE, &target_temperature[0], 0, HEATER_0_MAXTEMP - 5);
    MENU_ITEM_EDIT(int3, MSG_BED, &target_temperature_bed, 0, BED_MAXTEMP - 15);
    MENU_ITEM_EDIT(int3, MSG_FAN_SPEED, &fanSpeed, 0, 255);
    END_MENU();
}

////f3
////fH



#if SDCARDDETECT == -1
static void lcd_sd_refresh()
{
    card.initsd();
    currentMenuViewOffset = 0;
}
#endif
static void lcd_sd_updir()
{
    card.updir();
    currentMenuViewOffset = 0;
}

void lcd_sdcard_menu()
{
    if (lcdDrawUpdate == 0 && LCD_CLICKED == 0)
        return;	// nothing to do (so don't thrash the SD card)
    uint16_t fileCnt = card.getnrfilenames();
    START_MENU();
    MENU_ITEM(back, MSG_MAIN, lcd_main_menu);
    card.getWorkDirName();
    if(card.filename[0]=='/')
    {
#if SDCARDDETECT == -1
        MENU_ITEM(function, LCD_STR_REFRESH MSG_REFRESH, lcd_sd_refresh);
#endif
    }else{
        MENU_ITEM(function, LCD_STR_FOLDER "..", lcd_sd_updir);
    }

    for(uint16_t i=0;i<fileCnt;i++)
    {
        if (_menuItemNr == _lineNr)
        {
            #ifndef SDCARD_RATHERRECENTFIRST
              card.getfilename(i);
            #else
              card.getfilename(fileCnt-1-i);
            #endif
            if (card.filenameIsDir)
            {
                MENU_ITEM(sddirectory, MSG_CARD_MENU, card.filename, card.longFilename);
            }else{
                MENU_ITEM(sdfile, MSG_CARD_MENU, card.filename, card.longFilename);
            }
        }else{
            MENU_ITEM_DUMMY();
        }
    }
    END_MENU();
}

#define menu_edit_type(_type, _name, _strFunc, scale) \
    void menu_edit_ ## _name () \
    { \
        if ((int32_t)encoderPosition < 0) encoderPosition = 0; \
        if ((int32_t)encoderPosition > maxEditValue) encoderPosition = maxEditValue; \
        if (lcdDrawUpdate) \
            lcd_implementation_drawedit(editLabel, _strFunc(((_type)((int32_t)encoderPosition + minEditValue)) / scale)); \
        if (LCD_CLICKED) \
        { \
            *((_type*)editValue) = ((_type)((int32_t)encoderPosition + minEditValue)) / scale; \
            lcd_goto_menu(prevMenu, prevEncoderPosition); \
        } \
    } \
    void menu_edit_callback_ ## _name () { \
        menu_edit_ ## _name (); \
        if (LCD_CLICKED) (*callbackFunc)(); \
    } \
    static void menu_action_setting_edit_ ## _name (const char* pstr, _type* ptr, _type minValue, _type maxValue) \
    { \
        prevMenu = currentMenu; \
        prevEncoderPosition = encoderPosition; \
         \
        lcdDrawUpdate = 2; \
        currentMenu = menu_edit_ ## _name; \
         \
        editLabel = pstr; \
        editValue = ptr; \
        minEditValue = minValue * scale; \
        maxEditValue = maxValue * scale - minEditValue; \
        encoderPosition = (*ptr) * scale - minEditValue; \
    }\
    static void menu_action_setting_edit_callback_ ## _name (const char* pstr, _type* ptr, _type minValue, _type maxValue, menuFunc_t callback) \
    { \
        prevMenu = currentMenu; \
        prevEncoderPosition = encoderPosition; \
         \
        lcdDrawUpdate = 2; \
        currentMenu = menu_edit_callback_ ## _name; \
         \
        editLabel = pstr; \
        editValue = ptr; \
        minEditValue = minValue * scale; \
        maxEditValue = maxValue * scale - minEditValue; \
        encoderPosition = (*ptr) * scale - minEditValue; \
        callbackFunc = callback;\
    }
menu_edit_type(int, int3, itostr3, 1)
menu_edit_type(float, float3, ftostr3, 1)
menu_edit_type(float, float32, ftostr32, 100)
menu_edit_type(float, float43, ftostr43, 1000)
menu_edit_type(float, float5, ftostr5, 0.01)
menu_edit_type(float, float51, ftostr51, 10)
menu_edit_type(float, float52, ftostr52, 100)
menu_edit_type(unsigned long, long5, ftostr5, 0.01)

#ifdef REPRAPWORLD_KEYPAD
	static void reprapworld_keypad_move_z_up() {
    encoderPosition = 1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_z();
  }
	static void reprapworld_keypad_move_z_down() {
    encoderPosition = -1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_z();
  }
	static void reprapworld_keypad_move_x_left() {
    encoderPosition = -1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_x();
  }
	static void reprapworld_keypad_move_x_right() {
    encoderPosition = 1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_x();
	}
	static void reprapworld_keypad_move_y_down() {
    encoderPosition = 1;
    move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
		lcd_move_y();
	}
	static void reprapworld_keypad_move_y_up() {
		encoderPosition = -1;
		move_menu_scale = REPRAPWORLD_KEYPAD_MOVE_STEP;
    lcd_move_y();
	}
	static void reprapworld_keypad_move_home() {
		enquecommand_P((PSTR("G28"))); // move all axis home
	}
#endif

/** End of menus **/

static void lcd_quick_feedback()
{
    lcdDrawUpdate = 2;
    blocking_enc = millis() + 500;
    lcd_implementation_quick_feedback();
}

/** Menu action functions **/
static void menu_action_back(menuFunc_t data) { lcd_goto_menu(data); }
static void menu_action_submenu(menuFunc_t data) { lcd_goto_menu(data); }
static void menu_action_gcode(const char* pgcode) { enquecommand_P(pgcode); }
static void menu_action_function(menuFunc_t data) { (*data)(); }
static void menu_action_sdfile(const char* filename, char* longFilename)
{
    char cmd[30];
    char* c;
    sprintf_P(cmd, PSTR("M23 %s"), filename);
    for(c = &cmd[4]; *c; c++)
        *c = tolower(*c);
    enquecommand(cmd);
    enquecommand_P(PSTR("M24"));
    lcd_return_to_status();
}
static void menu_action_sddirectory(const char* filename, char* longFilename)
{
    card.chdir(filename);
    encoderPosition = 0;
}
static void menu_action_setting_edit_bool(const char* pstr, bool* ptr)
{
    *ptr = !(*ptr);
}
static void menu_action_setting_edit_callback_bool(const char* pstr, bool* ptr, menuFunc_t callback)
{
	menu_action_setting_edit_bool(pstr, ptr);
	(*callback)();
}
#endif//ULTIPANEL

/** LCD API **/
void lcd_init()
{
    lcd_implementation_init();

#ifdef NEWPANEL
    SET_INPUT(BTN_EN1);
    SET_INPUT(BTN_EN2);
    WRITE(BTN_EN1,HIGH);
    WRITE(BTN_EN2,HIGH);
  #if BTN_ENC > 0
    SET_INPUT(BTN_ENC);
    WRITE(BTN_ENC,HIGH);
  #endif
  #ifdef REPRAPWORLD_KEYPAD
    pinMode(SHIFT_CLK,OUTPUT);
    pinMode(SHIFT_LD,OUTPUT);
    pinMode(SHIFT_OUT,INPUT);
    WRITE(SHIFT_OUT,HIGH);
    WRITE(SHIFT_LD,HIGH);
  #endif
#else  // Not NEWPANEL
  #ifdef SR_LCD_2W_NL // Non latching 2 wire shift register
     pinMode (SR_DATA_PIN, OUTPUT);
     pinMode (SR_CLK_PIN, OUTPUT);
  #elif defined(SHIFT_CLK)
     pinMode(SHIFT_CLK,OUTPUT);
     pinMode(SHIFT_LD,OUTPUT);
     pinMode(SHIFT_EN,OUTPUT);
     pinMode(SHIFT_OUT,INPUT);
     WRITE(SHIFT_OUT,HIGH);
     WRITE(SHIFT_LD,HIGH);
     WRITE(SHIFT_EN,LOW);
  #else
     #ifdef ULTIPANEL
     #error ULTIPANEL requires an encoder
     #endif
  #endif // SR_LCD_2W_NL
#endif//!NEWPANEL

#if defined (SDSUPPORT) && defined(SDCARDDETECT) && (SDCARDDETECT > 0)
    pinMode(SDCARDDETECT,INPUT);
    WRITE(SDCARDDETECT, HIGH);
    lcd_oldcardstatus = IS_SD_INSERTED;
#endif//(SDCARDDETECT > 0)
#ifdef LCD_HAS_SLOW_BUTTONS
    slow_buttons = 0;
#endif
    lcd_buttons_update();
#ifdef ULTIPANEL
    encoderDiff = 0;
#endif
}

void lcd_update()
{
    static unsigned long timeoutToStatus = 0;

    #ifdef LCD_HAS_SLOW_BUTTONS
    slow_buttons = lcd_implementation_read_slow_buttons(); // buttons which take too long to read in interrupt context
    #endif

    lcd_buttons_update();

    #if (SDCARDDETECT > 0)
    if((IS_SD_INSERTED != lcd_oldcardstatus && lcd_detected()))
    {
        lcdDrawUpdate = 2;
        lcd_oldcardstatus = IS_SD_INSERTED;
        lcd_implementation_init( // to maybe revive the LCD if static electricity killed it.
          #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT) && !defined(DOGLCD)
            currentMenu == lcd_status_screen
          #endif
        );

        if(lcd_oldcardstatus)
        {
            card.initsd();
            LCD_MESSAGEPGM(MSG_SD_INSERTED);
        }
        else
        {
            card.release();
            LCD_MESSAGEPGM(MSG_SD_REMOVED);
        }
    }
    #endif//CARDINSERTED

    if (lcd_next_update_millis < millis())
    {
#ifdef ULTIPANEL
		#ifdef REPRAPWORLD_KEYPAD
        	if (REPRAPWORLD_KEYPAD_MOVE_Z_UP) {
        		reprapworld_keypad_move_z_up();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_Z_DOWN) {
        		reprapworld_keypad_move_z_down();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_X_LEFT) {
        		reprapworld_keypad_move_x_left();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_X_RIGHT) {
        		reprapworld_keypad_move_x_right();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_Y_DOWN) {
        		reprapworld_keypad_move_y_down();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_Y_UP) {
        		reprapworld_keypad_move_y_up();
        	}
        	if (REPRAPWORLD_KEYPAD_MOVE_HOME) {
        		reprapworld_keypad_move_home();
        	}
		#endif
        if (abs(encoderDiff) >= ENCODER_PULSES_PER_STEP)
        {
            lcdDrawUpdate = 1;
            encoderPosition += encoderDiff / ENCODER_PULSES_PER_STEP;
            encoderDiff = 0;
            timeoutToStatus = millis() + LCD_TIMEOUT_TO_STATUS;
        }
        if (LCD_CLICKED)
            timeoutToStatus = millis() + LCD_TIMEOUT_TO_STATUS;
		if (pausa_display == 1)
		timeoutToStatus = millis() + LCD_TIMEOUT_TO_STATUS;
#endif//ULTIPANEL

#ifdef DOGLCD        // Changes due to different driver architecture of the DOGM display
        blink++;     // Variable for fan animation and alive dot
        u8g.firstPage();
        do
        {
            u8g.setFont(u8g_font_6x10_marlin);
            u8g.setPrintPos(125,0);
            if (blink % 2) u8g.setColorIndex(1); else u8g.setColorIndex(0); // Set color for the alive dot
            u8g.drawPixel(127,63); // draw alive dot
            u8g.setColorIndex(1); // black on white
            (*currentMenu)();
            if (!lcdDrawUpdate)  break; // Terminate display update, when nothing new to draw. This must be done before the last dogm.next()
        } while( u8g.nextPage() );
#else
        (*currentMenu)();
#endif

#ifdef LCD_HAS_STATUS_INDICATORS
        lcd_implementation_update_indicators();
#endif

#ifdef ULTIPANEL
        if(timeoutToStatus < millis() && currentMenu != lcd_status_screen)
        {
            lcd_return_to_status();
            lcdDrawUpdate = 2;
        }
#endif//ULTIPANEL
        if (lcdDrawUpdate == 2) lcd_implementation_clear();
        if (lcdDrawUpdate) lcdDrawUpdate--;
        lcd_next_update_millis = millis() + LCD_UPDATE_INTERVAL;
    }
}

void lcd_ignore_click(bool b)
{
    ignore_click = b;
    wait_for_unclick = false;
}

void lcd_finishstatus() {
  int len = strlen(lcd_status_message);
  if (len > 0) {
    while (len < LCD_WIDTH) {
      lcd_status_message[len++] = ' ';
    }
  }
  lcd_status_message[LCD_WIDTH] = '\0';
  #if defined(LCD_PROGRESS_BAR) && defined(SDSUPPORT) && !defined(DOGLCD)
    #if PROGRESS_MSG_EXPIRE > 0
      messageTick =
    #endif
    progressBarTick = millis();
  #endif
  lcdDrawUpdate = 2;

  #ifdef FILAMENT_LCD_DISPLAY
    message_millis = millis();  //get status message to show up for a while
  #endif
}
void lcd_setstatus(const char* message)
{
    if (lcd_status_message_level > 0)
        return;
    strncpy(lcd_status_message, message, LCD_WIDTH);
    lcd_finishstatus();
}
void lcd_setstatuspgm(const char* message)
{
    if (lcd_status_message_level > 0)
        return;
    strncpy_P(lcd_status_message, message, LCD_WIDTH);
    lcd_finishstatus();
}
void lcd_setalertstatuspgm(const char* message)
{
    lcd_setstatuspgm(message);
    lcd_status_message_level = 1;
#ifdef ULTIPANEL
    lcd_return_to_status();
#endif//ULTIPANEL
}
void lcd_reset_alert_level()
{
    lcd_status_message_level = 0;
}

#ifdef DOGLCD
void lcd_setcontrast(uint8_t value)
{
    lcd_contrast = value & 63;
    u8g.setContrast(lcd_contrast);
}
#endif

#ifdef ULTIPANEL
/* Warning: This function is called from interrupt context */
void lcd_buttons_update()
{
#ifdef NEWPANEL
    uint8_t newbutton=0;
    if(READ(BTN_EN1)==0)  newbutton|=EN_A;
    if(READ(BTN_EN2)==0)  newbutton|=EN_B;
  #if BTN_ENC > 0
    if((blocking_enc<millis()) && (READ(BTN_ENC)==0))
        newbutton |= EN_C;
  #endif
    buttons = newbutton;
    #ifdef LCD_HAS_SLOW_BUTTONS
    buttons |= slow_buttons;
    #endif
    #ifdef REPRAPWORLD_KEYPAD
      // for the reprapworld_keypad
      uint8_t newbutton_reprapworld_keypad=0;
      WRITE(SHIFT_LD,LOW);
      WRITE(SHIFT_LD,HIGH);
      for(int8_t i=0;i<8;i++) {
          newbutton_reprapworld_keypad = newbutton_reprapworld_keypad>>1;
          if(READ(SHIFT_OUT))
              newbutton_reprapworld_keypad|=(1<<7);
          WRITE(SHIFT_CLK,HIGH);
          WRITE(SHIFT_CLK,LOW);
      }
      buttons_reprapworld_keypad=~newbutton_reprapworld_keypad; //invert it, because a pressed switch produces a logical 0
	#endif
#else   //read it from the shift register
    uint8_t newbutton=0;
    WRITE(SHIFT_LD,LOW);
    WRITE(SHIFT_LD,HIGH);
    unsigned char tmp_buttons=0;
    for(int8_t i=0;i<8;i++)
    {
        newbutton = newbutton>>1;
        if(READ(SHIFT_OUT))
            newbutton|=(1<<7);
        WRITE(SHIFT_CLK,HIGH);
        WRITE(SHIFT_CLK,LOW);
    }
    buttons=~newbutton; //invert it, because a pressed switch produces a logical 0
#endif//!NEWPANEL

    //manage encoder rotation
    uint8_t enc=0;
    if (buttons & EN_A) enc |= B01;
    if (buttons & EN_B) enc |= B10;
    if(enc != lastEncoderBits)
    {
        switch(enc)
        {
        case encrot0:
            if(lastEncoderBits==encrot3)
                encoderDiff++;
            else if(lastEncoderBits==encrot1)
                encoderDiff--;
            break;
        case encrot1:
            if(lastEncoderBits==encrot0)
                encoderDiff++;
            else if(lastEncoderBits==encrot2)
                encoderDiff--;
            break;
        case encrot2:
            if(lastEncoderBits==encrot1)
                encoderDiff++;
            else if(lastEncoderBits==encrot3)
                encoderDiff--;
            break;
        case encrot3:
            if(lastEncoderBits==encrot2)
                encoderDiff++;
            else if(lastEncoderBits==encrot0)
                encoderDiff--;
            break;
        }
    }
    lastEncoderBits = enc;
}

bool lcd_detected(void)
{
#if (defined(LCD_I2C_TYPE_MCP23017) || defined(LCD_I2C_TYPE_MCP23008)) && defined(DETECT_DEVICE)
  return lcd.LcdDetected() == 1;
#else
  return true;
#endif
}

void lcd_buzz(long duration, uint16_t freq)
{
#ifdef LCD_USE_I2C_BUZZER
  lcd.buzz(duration,freq);
#endif
}

bool lcd_clicked()
{
  return LCD_CLICKED;
}
#endif//ULTIPANEL

/********************************/
/** Float conversion utilities **/
/********************************/
//  convert float to string with +123.4 format
char conv[8];
char *ftostr3(const float &x)
{
  return itostr3((int)x);
}

char *itostr2(const uint8_t &x)
{
  //sprintf(conv,"%5.1f",x);
  int xx=x;
  conv[0]=(xx/10)%10+'0';
  conv[1]=(xx)%10+'0';
  conv[2]=0;
  return conv;
}

// Convert float to string with 123.4 format, dropping sign
char *ftostr31(const float &x)
{
  int xx=x*10;
  conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[1]=(xx/1000)%10+'0';
  conv[2]=(xx/100)%10+'0';
  conv[3]=(xx/10)%10+'0';
  conv[4]='.';
  conv[5]=(xx)%10+'0';
  conv[6]=0;
  return conv;
}

// Convert float to string with 123.4 format
char *ftostr31ns(const float &x)
{
  int xx=x*10;
  //conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[0]=(xx/1000)%10+'0';
  conv[1]=(xx/100)%10+'0';
  conv[2]=(xx/10)%10+'0';
  conv[3]='.';
  conv[4]=(xx)%10+'0';
  conv[5]=0;
  return conv;
}

char *ftostr32(const float &x)
{
  long xx=x*100;
  if (xx >= 0)
    conv[0]=(xx/10000)%10+'0';
  else
    conv[0]='-';
  xx=abs(xx);
  conv[1]=(xx/1000)%10+'0';
  conv[2]=(xx/100)%10+'0';
  conv[3]='.';
  conv[4]=(xx/10)%10+'0';
  conv[5]=(xx)%10+'0';
  conv[6]=0;
  return conv;
}

// Convert float to string with 1.234 format
char *ftostr43(const float &x)
{
	long xx = x * 1000;
    if (xx >= 0)
		conv[0] = (xx / 1000) % 10 + '0';
	else
		conv[0] = '-';
	xx = abs(xx);
	conv[1] = '.';
	conv[2] = (xx / 100) % 10 + '0';
	conv[3] = (xx / 10) % 10 + '0';
	conv[4] = (xx) % 10 + '0';
	conv[5] = 0;
	return conv;
}

//Float to string with 1.23 format
char *ftostr12ns(const float &x)
{
  long xx=x*100;
  
  xx=abs(xx);
  conv[0]=(xx/100)%10+'0';
  conv[1]='.';
  conv[2]=(xx/10)%10+'0';
  conv[3]=(xx)%10+'0';
  conv[4]=0;
  return conv;
}

//  convert float to space-padded string with -_23.4_ format
char *ftostr32sp(const float &x) {
  long xx = abs(x * 100);
  uint8_t dig;

  if (x < 0) { // negative val = -_0
    conv[0] = '-';
    dig = (xx / 1000) % 10;
    conv[1] = dig ? '0' + dig : ' ';
  }
  else { // positive val = __0
    dig = (xx / 10000) % 10;
    if (dig) {
      conv[0] = '0' + dig;
      conv[1] = '0' + (xx / 1000) % 10;
    }
    else {
      conv[0] = ' ';
      dig = (xx / 1000) % 10;
      conv[1] = dig ? '0' + dig : ' ';
    }
  }

  conv[2] = '0' + (xx / 100) % 10; // lsd always

  dig = xx % 10;
  if (dig) { // 2 decimal places
    conv[5] = '0' + dig;
    conv[4] = '0' + (xx / 10) % 10;
    conv[3] = '.';
  }
  else { // 1 or 0 decimal place
    dig = (xx / 10) % 10;
    if (dig) {
      conv[4] = '0' + dig;
      conv[3] = '.';
    }
    else {
      conv[3] = conv[4] = ' ';
    }
    conv[5] = ' ';
  }
  conv[6] = '\0';
  return conv;
}

char *itostr31(const int &xx)
{
  conv[0]=(xx>=0)?'+':'-';
  conv[1]=(xx/1000)%10+'0';
  conv[2]=(xx/100)%10+'0';
  conv[3]=(xx/10)%10+'0';
  conv[4]='.';
  conv[5]=(xx)%10+'0';
  conv[6]=0;
  return conv;
}

// Convert int to rj string with 123 or -12 format
char *itostr3(const int &x)
{
  int xx = x;
  if (xx < 0) {
     conv[0]='-';
     xx = -xx;
  } else if (xx >= 100)
    conv[0]=(xx/100)%10+'0';
  else
    conv[0]=' ';
  if (xx >= 10)
    conv[1]=(xx/10)%10+'0';
  else
    conv[1]=' ';
  conv[2]=(xx)%10+'0';
  conv[3]=0;
  return conv;
}

// Convert int to lj string with 123 format
char *itostr3left(const int &xx)
{
  if (xx >= 100)
  {
    conv[0]=(xx/100)%10+'0';
    conv[1]=(xx/10)%10+'0';
    conv[2]=(xx)%10+'0';
    conv[3]=0;
  }
  else if (xx >= 10)
  {
    conv[0]=(xx/10)%10+'0';
    conv[1]=(xx)%10+'0';
    conv[2]=0;
  }
  else
  {
    conv[0]=(xx)%10+'0';
    conv[1]=0;
  }
  return conv;
}

// Convert int to rj string with 1234 format
char *itostr4(const int &xx) {
  conv[0] = xx >= 1000 ? (xx / 1000) % 10 + '0' : ' ';
  conv[1] = xx >= 100 ? (xx / 100) % 10 + '0' : ' ';
  conv[2] = xx >= 10 ? (xx / 10) % 10 + '0' : ' ';
  conv[3] = xx % 10 + '0';
  conv[4] = 0;
  return conv;
}

// Convert float to rj string with 12345 format
char *ftostr5(const float &x) {
  long xx = abs(x);
  conv[0] = xx >= 10000 ? (xx / 10000) % 10 + '0' : ' ';
  conv[1] = xx >= 1000 ? (xx / 1000) % 10 + '0' : ' ';
  conv[2] = xx >= 100 ? (xx / 100) % 10 + '0' : ' ';
  conv[3] = xx >= 10 ? (xx / 10) % 10 + '0' : ' ';
  conv[4] = xx % 10 + '0';
  conv[5] = 0;
  return conv;
}

// Convert float to string with +1234.5 format
char *ftostr51(const float &x)
{
  long xx=x*10;
  conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[1]=(xx/10000)%10+'0';
  conv[2]=(xx/1000)%10+'0';
  conv[3]=(xx/100)%10+'0';
  conv[4]=(xx/10)%10+'0';
  conv[5]='.';
  conv[6]=(xx)%10+'0';
  conv[7]=0;
  return conv;
}

// Convert float to string with +123.45 format
char *ftostr52(const float &x)
{
  long xx=x*100;
  conv[0]=(xx>=0)?'+':'-';
  xx=abs(xx);
  conv[1]=(xx/10000)%10+'0';
  conv[2]=(xx/1000)%10+'0';
  conv[3]=(xx/100)%10+'0';
  conv[4]='.';
  conv[5]=(xx/10)%10+'0';
  conv[6]=(xx)%10+'0';
  conv[7]=0;
  return conv;
}

// Callback for after editing PID i value
// grab the pid i value out of the temp variable; scale it; then update the PID driver
void copy_and_scalePID_i()
{
#ifdef PIDTEMP
  Ki = scalePID_i(raw_Ki);
  updatePID();
#endif
}

// Callback for after editing PID d value
// grab the pid d value out of the temp variable; scale it; then update the PID driver
void copy_and_scalePID_d()
{
#ifdef PIDTEMP
  Kd = scalePID_d(raw_Kd);
  updatePID();
#endif
}

