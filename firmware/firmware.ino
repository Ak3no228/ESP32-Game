#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <GyverButton.h> 
#include "bitmaps.h"
#include <SD.h>
#include <BluetoothA2DPSource.h>

// hardware
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_UPDATE_DELAY 15
#define LEFT_BUTTON_PIN 26
#define RIGHT_BUTTON_PIN 25
#define UP_BUTTON_PIN 32
#define DOWN_BUTTON_PIN 33
#define CENTRAL_BUTTON_PIN 27
#define BAT_VCC_PIN 35
#define SD_PIN 5



// menu
#define MANIA    0
#define SNAKE    1



// mania
#define FRAME_TIME 16 
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
#define MAX_MISSES 3


// mania
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

const int MAX_FOLDERS = 20; 
String folderNames[MAX_FOLDERS];
int folderCount = 0;      
int current_map_index = 0;            


// music
File musicFile;
const int WAV_HEADER_SIZE = 44;
const int SAMPLE_RATE = 44100;
const int BUFFER_SAMPLES = 256;
static int16_t buffer[BUFFER_SAMPLES];
bool music_started = false;
bool BluetoothConnected = false;


// snake
#define MAX_LENGTH 50
int snakeScore = 0;
int snakeX[MAX_LENGTH];
int snakeY[MAX_LENGTH];
int snakeLength = 3;
int snakeDirection = 2;
int foodX = 40;
int foodY = 30;
unsigned long lastMove = 0;
int snakeSpeed = 150;

// menu
bool isMenu = true;
int menu_position = 0;
const char* MENU_ITEMS[] = {"MANIA", "SNAKE"};
const int NUM_MENU_ITEMS = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
const uint8_t MENU_ITEMS_OFFSETS[] = {34, 34};
int percentage = -1;
bool map_selection = true;


// classes
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1, 1000000, 400000);
GButton LEFT_BUTTON(LEFT_BUTTON_PIN), RIGHT_BUTTON(RIGHT_BUTTON_PIN), UP_BUTTON(UP_BUTTON_PIN),
        DOWN_BUTTON(DOWN_BUTTON_PIN), CENTRAL_BUTTON(CENTRAL_BUTTON_PIN);
BluetoothA2DPSource a2dp_source;

void setBright(uint16_t value);
void updateBatery();
void updateMenu();
void updateNotes(String fullPath);
void loopMenu();
void processMANIA();
void updateMapList();

void spawnFood();
void processSNAKE();

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

void connectionStatusChanged(esp_a2d_connection_state_t state, void *ptr) {
    switch (state) {
        case ESP_A2D_CONNECTION_STATE_CONNECTED:
            BluetoothConnected = true;
            break;
        case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
            BluetoothConnected = false;
            break;
        default:
            break;
    }
    if (isMenu)
    {
      updateMenu();
    }
}

void musicTask(void *parameter) {
  while (true) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}


void setup() {  
  LEFT_BUTTON.setDebounce(30);
  RIGHT_BUTTON.setDebounce(30);
  UP_BUTTON.setDebounce(30);
  DOWN_BUTTON.setDebounce(30);
  CENTRAL_BUTTON.setDebounce(30);

  LEFT_BUTTON.setStepTimeout(1); 
  RIGHT_BUTTON.setStepTimeout(1); 
  UP_BUTTON.setStepTimeout(1); 
  DOWN_BUTTON.setStepTimeout(1); 
  CENTRAL_BUTTON.setStepTimeout(1); 

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    while (1);
  }
  setBright(1);
  display.setTextSize(2);
  display.setTextWrap(false);
  display.setTextColor(SSD1306_WHITE);
  updateBatery();
  updateMenu();

  if (!SD.begin(SD_PIN)) {
    while (1);
  }
  
  updateMapList();
  
  a2dp_source.set_auto_reconnect(false);
  a2dp_source.set_data_callback_in_frames(get_data_frames);
  a2dp_source.set_on_connection_state_changed(connectionStatusChanged);
  a2dp_source.set_volume(25);
  a2dp_source.start("realme Buds Air 5 Pro"); 

  xTaskCreatePinnedToCore(
    musicTask,
    "Music Task",    4096,
    NULL,
    1,
    NULL,
    0
  );

  // snake
  randomSeed(analogRead(0));
  for (int i = 0; i < snakeLength; i++) {
    snakeX[i] = 20 - i * 4;
    snakeY[i] = 20;
  }
  spawnFood();

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
    if (menu_position == MANIA){
      if (map_selection == true){
        if (mapSelection()){
          map_selection = false;
          if (!enterMania()){
            isMenu = true;
          }
        }
      }
      else
      {
        processMANIA();
      }
    }
    else if (menu_position == SNAKE)
    {
      processSNAKE();
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
    if (menu_position==0)
    {
      map_selection = true;
    }
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
  if (BluetoothConnected)
  {
    display.setCursor(5, 3 );
    display.print("Connected");
  }
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
  float voltage = temp;;

  if (voltage <= 4.2)
  {
    percentage = map(voltage * 1000, 3000, 4200, 0, 100);
    percentage = constrain(percentage, 0, 100);
  }
  else percentage = -1;
}


void setBright(uint16_t value)
{
   display.ssd1306_command(SSD1306_SETCONTRAST);
   display.ssd1306_command(value);
}

bool mapSelection() {
  if (UP_BUTTON.isClick()) {
    current_map_index--;
    if (current_map_index < 0) current_map_index = folderCount - 1;
  }

  if (DOWN_BUTTON.isClick()) {
    current_map_index++;
    if (current_map_index >= folderCount) current_map_index = 0;
  }

  if (CENTRAL_BUTTON.isClick()) {
    return true;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  const int itemsPerPage = 4;
  int pageStart = (current_map_index / itemsPerPage) * itemsPerPage;

  for (int i = 0; i < itemsPerPage; i++) {
    int index = pageStart + i;
    if (index >= folderCount) break;

    display.setCursor(0, i * 16);

    if (index == current_map_index) {
      display.print("> ");
    } else {
      display.print("  ");
    }

    display.println(folderNames[index]);
  }
  display.display();
  return false;
}


bool enterMania()
{
  current_hitmark = -1;
  combo = 0;
  miss_count = 0; 

  if (folderCount==0) return false;

  String current_map_folder = "/maps/" + folderNames[current_map_index];
  String map_txt = current_map_folder+"/map.txt";
  updateNotes(map_txt);
  

  File root = SD.open(current_map_folder);
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
  music_started = true;
  return true; 
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
  display.setTextSize(1);
  enterMania();
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


void draw_note(bool line, int16_t x_position)
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
    clearButtons();
    updateMenu();
    return;
  }  
  display.clearDisplay();
  
  bool left_pressed = UP_BUTTON.isPress();
  bool right_pressed = DOWN_BUTTON.isPress();
  bool some_nots = false;
  
  for (int i = 0; i < note_count; i++) {
     Note &n = notes[i];
     n.type = 0;
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


void updateMapList()
{
  const char* path = "/maps";
  File root = SD.open(path);
  if (!root || !root.isDirectory()) {
    return;
  }
  folderCount = 0;
  File entry = root.openNextFile();

  while (entry && folderCount < MAX_FOLDERS) {
    if (entry.isDirectory()) {
      folderNames[folderCount] = String(entry.name());
      folderCount++;
    }
    entry = root.openNextFile();
  }
}


void updateNotes(String fullPath)
{
    note_count = 0;
    File file = SD.open(fullPath.c_str());
    if (!file) {
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
      }
    }
}

void spawnFood() {
  bool valid = false;
  while (!valid) {
    foodX = random(0, SCREEN_WIDTH / 4) * 4;
    foodY = random(0, SCREEN_HEIGHT / 4) * 4;
    valid = true;
    for (int i = 0; i < snakeLength; i++) {
      if (snakeX[i] == foodX && snakeY[i] == foodY) {
        valid = false;
        break;
      }
    }
  }
}

void processSNAKE() {
  if (LEFT_BUTTON.isClick() && snakeDirection != 2)  snakeDirection = 0;
  if (UP_BUTTON.isClick() && snakeDirection != 3)    snakeDirection = 1;
  if (RIGHT_BUTTON.isClick() && snakeDirection != 0) snakeDirection = 2;
  if (DOWN_BUTTON.isClick() && snakeDirection != 1)  snakeDirection = 3;

  if (millis() - lastMove > snakeSpeed) {
    lastMove = millis();

    for (int i = snakeLength - 1; i > 0; i--) {
      snakeX[i] = snakeX[i - 1];
      snakeY[i] = snakeY[i - 1];
    }
    
    if (snakeDirection == 0) snakeX[0] -= 4;
    if (snakeDirection == 1) snakeY[0] -= 4;
    if (snakeDirection == 2) snakeX[0] += 4;
    if (snakeDirection == 3) snakeY[0] += 4;

    if (snakeX[0] == foodX && snakeY[0] == foodY) {
      if (snakeLength < MAX_LENGTH) {
        snakeX[snakeLength] = snakeX[snakeLength - 1];
        snakeY[snakeLength] = snakeY[snakeLength - 1];
        snakeLength++;
      }
      snakeScore++;
      spawnFood();
    }
    
    if (snakeX[0] < 0 || snakeX[0] >= SCREEN_WIDTH ||
        snakeY[0] < 0 || snakeY[0] >= SCREEN_HEIGHT) {
      SnakeGameOver();
      return;
    }
    
    for (int i = 1; i < snakeLength; i++) {
      if (snakeX[0] == snakeX[i] && snakeY[0] == snakeY[i]) {
        SnakeGameOver();
        return;
      }
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0, 0);
    display.print("Score: ");
    display.print(snakeScore);
    display.fillRect(foodX, foodY, 4, 4, WHITE);

    for (int i = 0; i < snakeLength; i++) {
      display.fillRect(snakeX[i], snakeY[i], 4, 4, WHITE);
    }

    display.display();
  }
}

void SnakeGameOver() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE);
  display.setCursor(10, 25);
  display.println("Game Over");
  display.display();
  while (!CENTRAL_BUTTON.isClick())
  {
    tickButtons();
  }

  for (int i = 0; i < snakeLength; i++) {
    snakeX[i] = 20 - i * 4;
    snakeY[i] = 20;
  }
  spawnFood();
  snakeScore = 0;
  snakeLength = 3;
  snakeDirection = 2;
  clearButtons();
  updateMenu();
  isMenu = true;
}
