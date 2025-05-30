#include <Wire.h>
#include <SD.h>
#include <Adafruit_SSD1306.h>
#include <GyverButton.h> 
#include <BluetoothA2DPSource.h>
#include "bitmaps.h"


// hardware
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_UPDATE_DELAY 17 //60fps
#define LEFT_BUTTON_PIN 26
#define RIGHT_BUTTON_PIN 25
#define UP_BUTTON_PIN 32
#define DOWN_BUTTON_PIN 33
#define CENTRAL_BUTTON_PIN 27
#define BAT_VCC_PIN 35
#define SD_PIN 5



// menu
#define MANIA    0
#define BT_LINK  1
#define SETTINGS 2



// mania
#define FRAME_TIME 16 //80fps 
#define NOTE 1
#define SLIDER 2
#define NICE_HIT 2
#define BAD_HIT 1
#define MISS_HIT 0
#define FIRST_LINE 0
#define SECOND_LINE 1
#define FIRST_LINE_Y 16
#define SECOND_LINE_Y 48
#define BMP_S 16
#define BMP_X 66
#define BMP_Y 24
#define KEY_RADIUS 11
#define KEY_X_POSITION 16
#define NOTE_RADIUS 9
#define FALL_SPEED 0.14f
#define PERFECT_HIT_RANGE 1.0
#define NORMAL_HIT_RANGE  2.8
#define MAX_MISSES 5


// game
int8_t current_hitmark = -1;  // hitmark: (-1)->don't draw  (0)->miss  (1)->bad hit  (2)->nice hit
unsigned long last_hitmark_time, lastFrameTime = 0;
uint16_t combo = 0;
float fall_speed = 0.14f;
uint8_t miss_count = 0; 
double start_time = 0;

struct Note {
  bool type;              // 0->note, 1->slider
  bool line;              // 0->left, 1->right
  unsigned long time;     // in milliseconds
  unsigned long end_time; // only for sliders
  bool draw;
};

const int max_note_count = 800;
Note notes[max_note_count];
int note_count = 0;


//music
File musicFile;
const int WAV_HEADER_SIZE = 44;
const int SAMPLE_RATE = 44100;
const int BUFFER_SAMPLES = 256;
//static int16_t buffer[BUFFER_SAMPLES * 2];
static int16_t buffer[BUFFER_SAMPLES];
bool music_started = false;


// menu
bool isMenu = true;
int menu_position = 0;
const char* MENU_ITEMS[] = {"MANIA", "BT LINK", "SETTINGS"};
const int NUM_MENU_ITEMS = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
const uint8_t MENU_ITEMS_OFFSETS[] = {34, 22, 16};
int percentage = -1;
bool game_pad = false;


// classes
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, 1000000, 400000);
GButton LEFT_BUTTON(LEFT_BUTTON_PIN), RIGHT_BUTTON(RIGHT_BUTTON_PIN), UP_BUTTON(UP_BUTTON_PIN),
        DOWN_BUTTON(DOWN_BUTTON_PIN), CENTRAL_BUTTON(CENTRAL_BUTTON_PIN);
BluetoothA2DPSource a2dp_source;

void setBright(uint16_t value);
void updateBatery();
void updateMenu();
void updateNotes();
void loopMenu();
void processMANIA();

int32_t get_data_frames(Frame *frame, int32_t frame_count) {
    if (!musicFile || !musicFile.available()) return 0;
    if (!music_started) return 0;

    size_t bytesToRead = frame_count * sizeof(int16_t);
    size_t bytesRead = musicFile.read((uint8_t*)buffer, bytesToRead);

    int samplesRead = bytesRead / sizeof(int16_t);
    for (int i = 0; i < samplesRead; ++i) {
        int16_t sample = buffer[i];
        frame[i].channel1 = sample;
        frame[i].channel2 = sample;
    }

    if (!musicFile.available()) {
        musicFile.seek(WAV_HEADER_SIZE);
    }

    return samplesRead;
}

void musicTask(void *parameter) {
  while (true) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}


void setup() {
  Serial.begin(115200);
  
  LEFT_BUTTON.setDebounce(20);
  RIGHT_BUTTON.setDebounce(20);
  UP_BUTTON.setDebounce(20);
  DOWN_BUTTON.setDebounce(20);
  CENTRAL_BUTTON.setDebounce(20);

  LEFT_BUTTON.setStepTimeout(1); 
  RIGHT_BUTTON.setStepTimeout(1); 
  UP_BUTTON.setStepTimeout(1); 
  DOWN_BUTTON.setStepTimeout(1); 
  CENTRAL_BUTTON.setStepTimeout(1); 

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 allocation failed");
    while (1);
  }
  setBright(1);
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  updateBatery();
  updateMenu();

  if (!SD.begin(SD_PIN)) {
    Serial.println("Ошибка SD-карты.");
    while (1);
  }
  updateNotes();

  File root = SD.open("/maps/THE_ORAL_CIGARETTES_Kyouran_Hey_Kids!!");
  while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (name.endsWith(".wav") || name.endsWith(".WAV")) {
                musicFile = entry;
                break;
            }
        }
        entry.close();
  }
  musicFile.seek(WAV_HEADER_SIZE);

  a2dp_source.set_auto_reconnect(false);
  a2dp_source.set_data_callback_in_frames(get_data_frames);
  a2dp_source.set_volume(20);
  a2dp_source.start("realme Buds Air 5 Pro"); 

  xTaskCreatePinnedToCore(
    musicTask,
    "Music Task",
    4096,
    NULL,
    1,
    NULL,
    0
  );

}
void tickButtons() {
    LEFT_BUTTON.tick();
    RIGHT_BUTTON.tick();
    UP_BUTTON.tick();
    DOWN_BUTTON.tick();
    CENTRAL_BUTTON.tick();
}

void loop() {
  tickButtons();
  if (isMenu) loopMenu();
  else
  {
    if (menu_position == MANIA) {
      processMANIA();
    }
    else isMenu = true;
  }
}


void clearButtons() {
    LEFT_BUTTON.resetStates();
    RIGHT_BUTTON.resetStates();
    UP_BUTTON.resetStates();
    DOWN_BUTTON.resetStates();
    CENTRAL_BUTTON.resetStates();
}


void loopSubMenu() {
  display.fillScreen(SSD1306_WHITE);  
  display.display(); 
  if (DOWN_BUTTON.isClick()) {
    isMenu = true;
    UP_BUTTON.resetStates();
    updateMenu();
  }
}


void loopMenu() {
  if (CENTRAL_BUTTON.isClick()) {
    isMenu = false;
    start_time = millis();
    display.setTextSize(1);
    clearButtons();
  }
  
  if (LEFT_BUTTON.isClick()) {
    if (menu_position > 0) menu_position--;
    else menu_position = NUM_MENU_ITEMS - 1;
    updateMenu();   
  }
  
  if (RIGHT_BUTTON.isClick()) {
    if (menu_position + 1 < NUM_MENU_ITEMS) menu_position++;
    else menu_position = 0;
    updateMenu(); 
  }
  updateBatery();
}


void updateMenu() {
  display.setTextSize(1);
  display.clearDisplay();
  display.setCursor(106, 3 );
  display.print(percentage);
  display.print("%");
  display.drawLine(0, 11, 128, 11, SSD1306_WHITE);
  display.setTextSize(2);
  display.setCursor(2, 29);
  display.print("<");
  display.setCursor(114, 29);
  display.print(">");
  display.setCursor(MENU_ITEMS_OFFSETS[menu_position], 29);
  display.print(MENU_ITEMS[menu_position]);
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.display(); 
}


void updateBatery()
{
  int raw = analogRead(BAT_VCC_PIN);
  float temp = (raw * 3.3 / 4095.0)/0.6897;
  float voltage = temp;//(raw * 3.3 / 4095.0)* 3.427;

  if (voltage <= 4.2)
  {
    percentage = map(voltage * 1000, 3000, 4200, 0, 100);
    percentage = constrain(percentage, 0, 100);
  }
  else percentage = 0;
}


void setBright(uint16_t value)
{
   display.ssd1306_command(SSD1306_SETCONTRAST);
   display.ssd1306_command(value);
}


// mania
void resetMania(bool win){
  display.clearDisplay();
  display.setRotation(1);
  display.setTextSize(2);
  display.setCursor(13, 25);
  if (win) display.print("WIN");
  else display.print("LOSE");
  display.setRotation(0);
  display.display();
  music_started = false;
  while (!CENTRAL_BUTTON.state())
  {
    tickButtons();
  }
  current_hitmark = -1;
  combo = 0;
  miss_count = 0; 
  //updateNotes();
  start_time = millis();
  clearButtons();
  isMenu = true;
  updateNotes();

  File root = SD.open("/maps/THE_ORAL_CIGARETTES_Kyouran_Hey_Kids!!");
  while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (name.endsWith(".wav") || name.endsWith(".WAV")) {
                musicFile = entry;
                break;
            }
        }
        entry.close();
  }
  musicFile.seek(WAV_HEADER_SIZE);
  start_time = millis();
}


void process_hit(uint8_t type)
{
  last_hitmark_time = millis();
  current_hitmark = type;
  if (current_hitmark==0)
  {
    combo = 0;
    miss_count++;
    if (miss_count>=MAX_MISSES)
    {
      resetMania(false);
    }
  }
  else
  {
    combo++;
    miss_count=0;
  }
}


void draw_note(bool line, uint8_t x_position)
{
  uint8_t y_pos = (line == 0) ? FIRST_LINE_Y : SECOND_LINE_Y;
  display.fillCircle(x_position, y_pos, NOTE_RADIUS, WHITE);
}


void draw_hitmark()
{
  if (millis()-last_hitmark_time>1000) current_hitmark = -1;
  else if (current_hitmark==MISS_HIT)  display.drawBitmap(BMP_X, BMP_Y, miss_bmp, BMP_S, BMP_S, SSD1306_WHITE);
  else if (current_hitmark==BAD_HIT)  display.drawBitmap(BMP_X, BMP_Y, good_bmp, BMP_S, BMP_S, SSD1306_WHITE);
  else if (current_hitmark==NICE_HIT)  display.drawBitmap(BMP_X, BMP_Y, perfect_bmp, BMP_S, BMP_S, SSD1306_WHITE);
}

void draw_hit_keys()
{
  //left key
  display.drawCircle(KEY_X_POSITION, FIRST_LINE_Y, KEY_RADIUS, WHITE);
  display.drawCircle(KEY_X_POSITION, FIRST_LINE_Y, KEY_RADIUS - 1, WHITE);
  //right key
  display.drawCircle(KEY_X_POSITION, SECOND_LINE_Y, KEY_RADIUS, WHITE);
  display.drawCircle(KEY_X_POSITION, SECOND_LINE_Y, KEY_RADIUS - 1, WHITE);
}

void processMANIA()
{
  music_started = true;
  unsigned long current_time = millis() - start_time;
  if (millis() - lastFrameTime < FRAME_TIME) return;

  if (RIGHT_BUTTON.isClick())
  {
    isMenu = true;
    music_started = false;
    current_hitmark = -1;
    combo = 0;
    miss_count = 0; 
    updateNotes();
    clearButtons();

    File root = SD.open("/maps/THE_ORAL_CIGARETTES_Kyouran_Hey_Kids!!");
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String name = entry.name();
            if (name.endsWith(".wav") || name.endsWith(".WAV")) {
                musicFile = entry;
                break;
            }
        }
        entry.close();
    }
    musicFile.seek(WAV_HEADER_SIZE);
    return;
  }  
  display.clearDisplay();
  
  bool left_pressed = UP_BUTTON.isPress();
  bool right_pressed = DOWN_BUTTON.isPress();


  bool some_nots = false;
  
  for (int i = 0; i < note_count; i++) {
     Note &n = notes[i];
     if (n.draw==false || n.type != 0) continue;
     some_nots = true;

     float x_pos = (((long)n.time - (long)current_time) * fall_speed) + KEY_X_POSITION;
     if (x_pos >= -NOTE_RADIUS && x_pos <= 128+NOTE_RADIUS && n.type == 0) {
        long time_to_hit = (long)current_time - (long)n.time;

        if ((left_pressed || right_pressed) && abs(time_to_hit) <= 100)
        {
          if (n.line == FIRST_LINE && left_pressed){
              left_pressed = false;
              if (abs(time_to_hit) <= 50) process_hit(NICE_HIT);
              else process_hit(BAD_HIT);
              n.draw = false;
          }
          else if (n.line == SECOND_LINE && right_pressed){
              right_pressed = false;
              if (abs(time_to_hit) <= 50) process_hit(NICE_HIT);
              else process_hit(BAD_HIT);
              n.draw = false;
          }
        }
        if (n.type == 0 && n.draw) {
          draw_note(n.line,  (int)x_pos);
        }
      }
      else if (x_pos<-NOTE_RADIUS && n.draw)
      {
          n.draw = false;
          process_hit(MISS_HIT);
      }
  }
  if (some_nots == false)
  {
    resetMania(true);
    return;
  }
  draw_hit_keys();
  draw_hitmark();
  if (combo>5)
  {
    display.setRotation(1);
    display.setCursor(7,4); 
    display.print(combo);
    display.print(F("X"));
    display.setRotation(0);
  }
  display.display();
  lastFrameTime = millis();
}


void updateNotes()
{
    String fullPath = "/maps/THE_ORAL_CIGARETTES_Kyouran_Hey_Kids!!/map.txt";
    File file = SD.open(fullPath.c_str());
    if (!file) {
      Serial.println("Файл не знайден: " + fullPath);
      return;
    }
    while (file.available() && note_count <= 800) {
      String line = file.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;
  
      int type, lineSide;
      unsigned long time, endTime;
  
      int matched = sscanf(line.c_str(), "%d %d %lu %lu", &type, &lineSide, &time, &endTime);
      if (matched == 4) {
        notes[note_count].type = type;
        notes[note_count].line = lineSide;
        notes[note_count].time = time+1000;
        notes[note_count].end_time = endTime;
        notes[note_count].draw = true;
        note_count++;
        Serial.println(lineSide);
      }
    }
    Serial.print("Кол-во нот: ");
    Serial.println(note_count);  
}
