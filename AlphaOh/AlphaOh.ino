/***************************************************
  This is a library for our I2C LED Backpacks

  Designed specifically to work with the Adafruit LED Matrix backpacks
  ----> http://www.adafruit.com/products/872
  ----> http://www.adafruit.com/products/871
  ----> http://www.adafruit.com/products/870

  These displays use I2C to communicate, 2 pins are required to
  interface. There are multiple selectable I2C addresses. For backpacks
  with 2 Address Select pins: 0x70, 0x71, 0x72 or 0x73. For backpacks
  with 3 Address Select pins: 0x70 thru 0x77

  Adafruit invests time and resources providing this open source code,
  please support Adafruit and open-source hardware by purchasing
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.
  BSD license, all text above must be included in any redistribution
 ****************************************************/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include "Adafruit_LEDBackpack.h"

//#define NODE_MCU_AMICA

// ------------------begin ESP8266'centric----------------------------------
#ifdef NODE_MCU_AMICA
#define FREQUENCY    160                  // valid 80, 160
//
#include "ESP8266WiFi.h"
extern "C" {
#include "user_interface.h"
}
#endif
// ------------------end ESP8266'centric------------------------------------

#ifdef NODE_MCU_AMICA
#define GPIO_D0  (16)
#define GPIO_D1  (5)
#define GPIO_D2  (4)
#define GPIO_D3  (0)
#define GPIO_D4  (2) //Module LED(Low Act.)
#define GPIO_D5  (14)
#define GPIO_D6  (12)
#define GPIO_D7  (13)
#define GPIO_D8  (15)
#define GPIO_D9  (3)
#define GPIO_D10 (1)
#define GPIO_D11 (9)
#define GPIO_D12 (10)
#endif

Adafruit_LEDBackpack matrix = Adafruit_LEDBackpack();

/* キーの種類定義用 */
enum eKEY {
  eKEY_OFF = 0,
  eKEY_UP,
  eKEY_LEFT,
  eKEY_ENTER,
  eKEY_RIGHT,
  eKEY_DOWN
};
#ifdef NODE_MCU_AMICA
const int upPin = GPIO_D4;  // キー定義 UP
const int leftPin = GPIO_D3;  // キー定義 LEFT
const int enterPin = GPIO_D2;  // キー定義 ENTER
const int rightPin = GPIO_D1;  // キー定義 RIGHT
const int downPin = GPIO_D0;  // キー定義 DOWN
#else
const int upPin = A0;  // キー定義 UP
const int leftPin = A1;  // キー定義 LEFT
const int enterPin = A2;  // キー定義 ENTER
const int rightPin = A3;  // キー定義 RIGHT
const int downPin = A4;  // キー定義 DOWN
#endif

#define BOARD_BLANK (0x00)
#define BOARD_BLACK (0x01) // 青
#define BOARD_WHITE (0x02) // 赤
#define BOARD_ENABLE (0x03)
#define BOARD_WIN (0x10)
#define BOARD_LOSE (0x20)
#define BOARD_DRAW (0x30)

static bool boRefleshEvent = false; // 表示の更新要求
static uint8_t displayBuffer[64] = {0}; // 表示用バッファ
static uint16_t matrixBuff0[8] = {0}; // MATRIX表示用バッファ(0) : 切り替えて使う
static uint16_t matrixBuff1[8] = {0}; // MATRIX表示用バッファ(1) : 切り替えて使う
static uint8_t enable_count = 0; // 置ける個数の管理
static uint8_t enables[64] = {0}; // 置ける場所管理用
static bool boKeyEvent = false; // キー確定イベント
static uint8_t actKey = eKEY_OFF; // 確定済みキー
static uint8_t selCell = 0xFF; /* 選んでいるセルのインデックス(enables[selCellIndex]) */
static uint8_t selCellIndex = 0xFF; /* 選んでいるセルのenables内におけるインデックス */

void setup() {
  /* Initializations */
  // ------------------begin ESP8266'centric----------------------------------
#ifdef NODE_MCU_AMICA
  WiFi.forceSleepBegin();                  // turn off ESP8266 RF
  delay(1);                                // give RF section time to shutdown
  system_update_cpu_freq(FREQUENCY);
#endif
  // ------------------end ESP8266'centric------------------------------------

  pinMode(upPin, INPUT_PULLUP);
  pinMode(leftPin, INPUT_PULLUP);
  pinMode(enterPin, INPUT_PULLUP);
  pinMode(rightPin, INPUT_PULLUP);
  pinMode(downPin, INPUT_PULLUP);

  Serial.begin(115200);
  Serial.println("Alpha Oh");

#ifdef NODE_MCU_AMICA
  Wire.pins(GPIO_D5, GPIO_D6);   /* SDA(D5), SCL(D6) */
#endif
  matrix.begin(0x70);  // pass in the address
  matrix.setBrightness(10);
}

static long frame_exec = 0; // フレーム周期管理用
static long key_exec = 0; // キー周期管理用
static uint8_t frame_count = 0;

void loop() {
  // ------------------begin ESP8266'centric----------------------------------
#ifdef NODE_MCU_AMICA
  // pat the watchdog timer
  wdt_reset();
#endif
  // ------------------end ESP8266'centric------------------------------------
  long now = millis();

  if (now - frame_exec > 100) { // 100ms周期のフレーム処理
    /* フレーム処理 */
    frame_exec = now;
    displayHandler();
  }

  if (now - key_exec > 20) { // 20ms周期のキー処理
    /* キー処理, 3回一致でキーを確定させる */
    key_exec = now;
    keyHandler();
  }
}

/*******************************************/
// 表示イベントハンドラ
/*******************************************/
void displayHandler() {
  frame_count++;
  frame_count %= 10;
  uint8_t x, y;

  if ( boRefleshEvent ) {
    /* 受信バッファに更新があった場合 */
    boRefleshEvent = false;

    /* 表示バッファをベースにMATRIXバッファを作成する */
    enable_count = 0;
    selCell = 0xFF;
    selCellIndex = 0xFF;

    memset(matrixBuff0, 0x00, sizeof(matrixBuff0));
    memset(matrixBuff1, 0x00, sizeof(matrixBuff1));
    for (int i = 0; i < 64; i++) {
      x = i / 8;
      y = i % 8;

      switch (displayBuffer[i]) {
        case BOARD_BLANK:
          {
            /* do nothing */
            break;
          }
        case BOARD_ENABLE:
          {
            enables[enable_count] = i;
            enable_count++;
            if (selCell == 0xFF) {
              selCellIndex = 0;
              selCell = i;
              /* バッファの点滅位置更新 */
              matrixBuff0[x] |= ((0b1 << (y + 8)) | (0b1 << y));
            }
            break;
          }
        case BOARD_BLACK: // 青(AI)
          {
            /* 上位bitのみ立てる */
            matrixBuff0[x] |= (0b1 << (y + 8));
            matrixBuff1[x] |= (0b1 << (y + 8));
            break;
          }
        case BOARD_WHITE: // 赤
          {
            /* 下位bitのみ立てる */
            matrixBuff0[x] |= (0b1 << y);
            matrixBuff1[x] |= (0b1 << y);
            break;
          }
        case BOARD_WIN:
          {
            /* WIN処理 */
            break;
          }
        case BOARD_LOSE:
          {
            /* LOSE処理 */
            break;
          }
        case BOARD_DRAW:
          {
            /* DRAW処理 */
            break;
          }
        default:
          {
            /* do nothing */
            break;
          }
      }
    }

    if ( enable_count == 0 ) {
      /* 置けるところがない場合 */
      Serial.write(0x80);
    }
  }

  if ( boKeyEvent ) {
    /* キーイベントに応じた表示の更新 */
    boKeyEvent = false;

    if (enable_count != 0) {
      /* 置けるところがある場合 */
      if ( actKey == eKEY_UP || actKey == eKEY_RIGHT) {
        selCellIndex = (selCellIndex + 1) % enable_count;
        selCell = enables[selCellIndex];

        /* バッファの点滅位置更新 */
        x = selCell / 8;
        y = selCell % 8;
        memcpy(matrixBuff0, matrixBuff1, sizeof(matrixBuff1)); // バッファの状態を揃える
        matrixBuff0[x] |= ((0b1 << (y + 8)) | (0b1 << y));
      } else if (actKey == eKEY_DOWN || actKey == eKEY_LEFT) {
        selCellIndex = (selCellIndex + enable_count - 1) % enable_count;
        selCell = enables[selCellIndex];

        /* バッファの点滅位置更新 */
        x = selCell / 8;
        y = selCell % 8;
        memcpy(matrixBuff0, matrixBuff1, sizeof(matrixBuff1)); // バッファの状態を揃える
        matrixBuff0[x] |= ((0b1 << (y + 8)) | (0b1 << y));
      } else if (actKey == eKEY_ENTER) {

        /* 確定イベント送信 */
        for (int i = 0; i < enable_count; i++) {
          x = enables[i] / 8;
          y = enables[i] % 8;
        }
        memcpy(matrixBuff0, matrixBuff1, sizeof(matrixBuff1)); // バッファの状態を揃える
        x = selCell / 8;
        y = selCell % 8;
        matrixBuff0[x] |= (0b1 << (y));
        matrixBuff1[x] |= (0b1 << (y));
        Serial.write(selCell);
      }
    }
  }
  if (frame_count % 10 < 4) {
    memcpy(matrix.displaybuffer, matrixBuff0, sizeof(matrixBuff0));
  } else {
    memcpy(matrix.displaybuffer, matrixBuff1, sizeof(matrixBuff1));
  }
  matrix.writeDisplay();
}

/*******************************************/
// シリアルイベントハンドラ
/*******************************************/
void serialEvent() {
  static uint8_t screen[64] = {0}; // 受信バッファ
  static uint8_t serCnt = 0; // screen一括取得管理用
  uint8_t ubBuff;

  while (Serial.available()) {
    // get the new byte:
    ubBuff = (uint8_t)Serial.read();
    screen[serCnt] = ubBuff;
    serCnt++;
    if (serCnt >= 64) {
      serCnt = 0;
      /* 64byte貯まった時の処理 */
      memcpy(displayBuffer, screen, 64); // 表示用バッファにコピー
      boRefleshEvent = true;
    }
  }
}

/*******************************************/
// キーイベントハンドラ
/*******************************************/
void keyHandler() {
  static uint8_t keyCnt = 0;
  static uint8_t chatKey = eKEY_OFF;
  uint8_t currentKey = eKEY_OFF;

  /* PINの読み込み */
  uint8_t key;
  key = digitalRead(upPin);
  if ( key != 1 ) {
    currentKey = eKEY_UP;
  }
  key = digitalRead(leftPin);
  if ( key != 1 ) {
    currentKey = eKEY_LEFT;
  }
  key = digitalRead(enterPin);
  if ( key != 1 ) {
    currentKey = eKEY_ENTER;
  }
  key = digitalRead(rightPin);
  if ( key != 1 ) {
    currentKey = eKEY_RIGHT;
  }
  key = digitalRead(downPin);
  if ( key != 1 ) {
    currentKey = eKEY_DOWN;
  }

  if (keyCnt == 0) {
    /* 新しいキーの場合 */
    chatKey = currentKey;
  }

  if (actKey != currentKey) {
    /* 確定済みのキーと違うキーの場合 */

    if (chatKey == currentKey) {
      keyCnt++;
    } else {
      keyCnt = 0;
      chatKey = eKEY_OFF;
    }
    if (keyCnt == 3) {
      /* キー確定 */
      actKey = currentKey;
      boKeyEvent = true;
      //      Serial.print("キー確定: ");
      //      Serial.println(actKey);
    }
  }
}

